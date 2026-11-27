package com.droidspaces.app.service

import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.ContentUris
import android.content.ContentValues
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.os.IBinder
import android.os.PowerManager
import android.os.SystemClock
import android.provider.MediaStore
import androidx.core.app.NotificationCompat
import androidx.core.app.ServiceCompat
import androidx.core.content.ContextCompat
import com.droidspaces.app.MainActivity
import com.droidspaces.app.R
import com.droidspaces.app.util.RootfsAsset
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.io.OutputStream
import java.net.HttpURLConnection
import java.net.URL
import kotlin.coroutines.cancellation.CancellationException
import kotlin.coroutines.coroutineContext

// Per-asset download state, keyed by download URL everywhere it is used
sealed class AssetDownloadState {
    data object Idle                        : AssetDownloadState()
    data class  Downloading(val percent: Int) : AssetDownloadState()
    data class  Done(val uri: Uri)          : AssetDownloadState()
    data class  Failed(val reason: String)  : AssetDownloadState()
}

/*
 * Downloads rootfs tarballs into public Downloads without the system DownloadManager. Some ROMs ship
 * without the Downloads provider (getSystemService returns null and we crashed), and on others its
 * JobScheduler backend never gets a network and sits at 0% (#329). A dataSync foreground service
 * keeps the transfer alive while the app is in the background, on every API level from minSdk up.
 *
 * ponytail: targetSdk 35 caps dataSync at 6h/24h, add onTimeout() -> stopSelf() when we bump it.
 */
class RootfsDownloadService : Service() {

    // Main dispatcher so jobs is only touched from one thread, the transfer itself hops to IO.
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)
    private val jobs = HashMap<String, Job>()
    private var wakeLock: PowerManager.WakeLock? = null
    private val nm by lazy { getSystemService(NotificationManager::class.java) }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        nm.createNotificationChannel(
            NotificationChannel(CHANNEL_ID, getString(R.string.repo_dl_channel_name), NotificationManager.IMPORTANCE_LOW)
        )
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val url = intent?.getStringExtra(EXTRA_URL)
        when (intent?.action) {
            ACTION_START -> {
                val name = intent.getStringExtra(EXTRA_NAME)
                // startForegroundService() demands this promptly, even if we end up doing nothing
                ServiceCompat.startForeground(
                    this, NOTIFICATION_ID, progressNotification(url.orEmpty(), name.orEmpty(), 0),
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC else 0
                )
                if (url != null && name != null) launch(url, name)
            }
            ACTION_CANCEL -> url?.let { jobs[it]?.cancel() }
        }
        stopIfIdle()
        return START_NOT_STICKY
    }

    override fun onDestroy() {
        scope.cancel()
        wakeLock?.let { if (it.isHeld) it.release() }
        super.onDestroy()
    }

    private fun launch(url: String, name: String) {
        if (wakeLock == null) {
            // Without it the CPU sleeps with the screen off and the socket stalls until readTimeout
            wakeLock = getSystemService(PowerManager::class.java)
                .newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "droidspaces:rootfs-download")
                .apply { acquire() }
        }
        // A Cancel tap followed by a quick Download can land while the old job is still
        // blocked in read(), so the old job must not touch state or bookkeeping once replaced.
        jobs[url]?.cancel()
        jobs[url] = scope.launch {
            val me = coroutineContext[Job]
            val current = { jobs[url] === me }
            try {
                // NotificationManager sheds anything past ~5 posts/s per package, and the done
                // notification posted right after a burst of progress updates got shed with them
                var lastNotify = 0L
                val uri = withContext(Dispatchers.IO) {
                    download(url, name) { pct ->
                        set(url, AssetDownloadState.Downloading(pct))
                        val now = SystemClock.elapsedRealtime()
                        if (now - lastNotify >= 1000) {
                            lastNotify = now
                            nm.notify(NOTIFICATION_ID, progressNotification(url, name, pct))
                        }
                    }
                }
                set(url, AssetDownloadState.Done(uri))
                nm.notify(url.hashCode(), doneNotification(name))
            } catch (e: CancellationException) {
                if (current()) set(url, null)
                throw e
            } catch (e: Exception) {
                set(url, AssetDownloadState.Failed(e.message ?: getString(R.string.repo_dl_error_unknown)))
            } finally {
                if (current()) {
                    jobs.remove(url)
                    stopIfIdle()
                }
            }
        }
    }

    private fun stopIfIdle() {
        if (jobs.isNotEmpty()) return
        wakeLock?.let { if (it.isHeld) it.release() }
        wakeLock = null
        ServiceCompat.stopForeground(this, ServiceCompat.STOP_FOREGROUND_REMOVE)
        stopSelf()
    }

    private suspend fun download(url: String, name: String, onProgress: (Int) -> Unit): Uri {
        // Same rule as RootfsRepository.httpGet: the rootfs supply chain must not be MITM-able
        if (!url.startsWith("https://", ignoreCase = true)) throw IOException(getString(R.string.repo_dl_error_unknown))
        val conn = (URL(url).openConnection() as HttpURLConnection).apply {
            connectTimeout = 15_000
            readTimeout = 30_000
        }
        try {
            when (val code = conn.responseCode) {
                HttpURLConnection.HTTP_OK -> Unit
                HttpURLConnection.HTTP_NOT_FOUND -> throw IOException(getString(R.string.repo_dl_error_not_found))
                else -> throw IOException(getString(R.string.repo_dl_error_code, code))
            }
            val total = conn.contentLengthLong
            val (uri, out) = openOutput(name)
            try {
                out.use { o ->
                    conn.inputStream.use { i ->
                        val buf = ByteArray(64 * 1024)
                        var done = 0L
                        var last = -1
                        while (true) {
                            coroutineContext.ensureActive()
                            val n = i.read(buf)
                            if (n < 0) break
                            o.write(buf, 0, n)
                            done += n
                            val pct = if (total > 0) (done * 100 / total).toInt() else 0
                            if (pct != last) {
                                last = pct
                                onProgress(pct)
                            }
                        }
                        if (total > 0 && done != total) throw IOException(getString(R.string.repo_dl_error_http_data))
                    }
                }
                if (uri.scheme == "content") {
                    contentResolver.update(uri, ContentValues().apply { put(MediaStore.Downloads.IS_PENDING, 0) }, null, null)
                }
                return uri
            } catch (e: Throwable) {
                // A half-written tarball would later look installable, never leave one behind
                if (uri.scheme == "content") runCatching { contentResolver.delete(uri, null, null) }
                else uri.path?.let { File(it).delete() }
                throw e
            }
        } finally {
            conn.disconnect()
        }
    }

    private fun openOutput(name: String): Pair<Uri, OutputStream> {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) {
            val file = publicFile(name).apply { parentFile?.mkdirs(); delete() }
            return Uri.fromFile(file) to FileOutputStream(file)
        }
        val collection = MediaStore.Downloads.EXTERNAL_CONTENT_URI
        // A row left pending by a killed process would make MediaStore rename us to "name (1)",
        // which findDownloaded() then never matches. Rows we don't own are skipped by the provider.
        runCatching { contentResolver.delete(collection, "${MediaStore.Downloads.DISPLAY_NAME}=?", arrayOf(name)) }
        val uri = contentResolver.insert(collection, ContentValues().apply {
            put(MediaStore.Downloads.DISPLAY_NAME, name)
            put(MediaStore.Downloads.IS_PENDING, 1)
        }) ?: throw IOException(getString(R.string.repo_dl_error_file_error))
        val out = contentResolver.openOutputStream(uri, "w")
        if (out == null) {
            contentResolver.delete(uri, null, null)
            throw IOException(getString(R.string.repo_dl_error_file_error))
        }
        return uri to out
    }

    private fun progressNotification(url: String, name: String, pct: Int) =
        builder(name, getString(R.string.repo_notification_description), MainActivity.ACTION_SHORTCUT_CONTAINERS)
            .setProgress(100, pct, pct == 0)
            .setOngoing(true)
            .addAction(
                0, getString(R.string.repo_cancel),
                PendingIntent.getService(
                    this, url.hashCode(),
                    Intent(this, RootfsDownloadService::class.java).setAction(ACTION_CANCEL).putExtra(EXTRA_URL, url),
                    PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
                )
            )
            .build()

    private fun doneNotification(name: String) =
        builder(name, getString(R.string.repo_dl_complete), MainActivity.ACTION_INSTALL_ROOTFS) {
            putExtra(MainActivity.EXTRA_ROOTFS_FILE, name)
        }
            .setAutoCancel(true)
            .build()

    // Tapping either notification opens the app, the finished one straight into the installer
    private fun builder(name: String, text: String, action: String, extras: Intent.() -> Unit = {}): NotificationCompat.Builder {
        val open = Intent(this, MainActivity::class.java)
            .setAction(action)
            .apply(extras)
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_launcher_foreground)
            .setContentTitle(name)
            .setContentText(text)
            .setOnlyAlertOnce(true)
            .setContentIntent(
                PendingIntent.getActivity(
                    this, name.hashCode(), open,
                    PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
                )
            )
    }

    companion object {
        private const val CHANNEL_ID = "droidspaces_rootfs_downloads"
        private const val NOTIFICATION_ID = 43
        private const val ACTION_START = "com.droidspaces.app.ROOTFS_DOWNLOAD_START"
        private const val ACTION_CANCEL = "com.droidspaces.app.ROOTFS_DOWNLOAD_CANCEL"
        private const val EXTRA_URL = "url"
        private const val EXTRA_NAME = "name"

        // Lives in the process, not the service, so the sheet can reopen and still see progress
        private val _states = MutableStateFlow<Map<String, AssetDownloadState>>(emptyMap())
        val states = _states.asStateFlow()

        fun start(ctx: Context, asset: RootfsAsset) {
            set(asset.downloadUrl, AssetDownloadState.Downloading(0))
            ContextCompat.startForegroundService(
                ctx,
                Intent(ctx, RootfsDownloadService::class.java)
                    .setAction(ACTION_START)
                    .putExtra(EXTRA_URL, asset.downloadUrl)
                    .putExtra(EXTRA_NAME, asset.uniqueFilename)
            )
        }

        fun cancel(ctx: Context, url: String) {
            set(url, null)
            ctx.startService(Intent(ctx, RootfsDownloadService::class.java).setAction(ACTION_CANCEL).putExtra(EXTRA_URL, url))
        }

        // The done notification is a shortcut into the installer, stale once the sheet got the user there
        fun dismissDone(ctx: Context, url: String) =
            ctx.getSystemService(NotificationManager::class.java).cancel(url.hashCode())

        fun set(url: String, state: AssetDownloadState?) =
            _states.update { if (state == null) it - url else it + (url to state) }

        fun findDownloaded(ctx: Context, name: String): Uri? {
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) {
                return publicFile(name).takeIf { it.length() > 0 }?.let(Uri::fromFile)
            }
            val collection = MediaStore.Downloads.EXTERNAL_CONTENT_URI
            // Pending rows (in-flight or killed mid-download) are excluded by default
            return ctx.contentResolver.query(
                collection, arrayOf(MediaStore.Downloads._ID),
                "${MediaStore.Downloads.DISPLAY_NAME}=?", arrayOf(name), null
            )?.use { if (it.moveToFirst()) ContentUris.withAppendedId(collection, it.getLong(0)) else null }
        }

        private fun publicFile(name: String) =
            File(Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS), name)
    }
}
