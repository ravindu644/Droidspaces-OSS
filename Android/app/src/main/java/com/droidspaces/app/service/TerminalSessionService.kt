package com.droidspaces.app.service

import android.app.*
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.content.pm.ServiceInfo
import android.os.Binder
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.PowerManager
import android.provider.Settings
import androidx.compose.runtime.mutableStateMapOf
import androidx.core.app.NotificationCompat
import com.droidspaces.app.MainActivity
import com.droidspaces.app.R
import com.droidspaces.app.ui.terminal.DroidspacesTerminalSession
import com.droidspaces.app.util.PreferencesManager
import com.droidspaces.app.ui.terminal.NoOpTerminalSessionClient
import com.termux.terminal.TerminalSession
import com.termux.terminal.TerminalSessionClient

data class SessionInfo(val containerName: String, val user: String, val fontSizePx: Int)

class TerminalSessionService : Service() {

    private val sessions = hashMapOf<String, TerminalSession>()

    private var wakeLock: PowerManager.WakeLock? = null

    inner class SessionBinder : Binder() {

        fun createSession(
            containerName: String,
            client: TerminalSessionClient,
            containerUser: String? = null,
            sessionId: String = containerName,
        ): TerminalSession {
            val user = containerUser ?: "root"
            return DroidspacesTerminalSession.create(
                sessionClient = client,
                containerName = containerName,
                containerUser = user,
            ).also {
                sessions[sessionId] = it
                globalSessionList[sessionId] = SessionInfo(
                    containerName,
                    user,
                    PreferencesManager.getInstance(this@TerminalSessionService).terminalFontSizePx
                )
                if (sessions.size == 1) {
                    promoteToForeground()
                } else {
                    updateNotification()
                }
            }
        }

        fun getSession(id: String): TerminalSession? = sessions[id]

        /**
         * Swap every session's client to the app-scoped no-op so a disposed terminal
         * screen (its TerminalBackEnd -> Activity/TerminalView) is not retained by this
         * long-lived service. Sessions keep running; the UI re-attaches its own client
         * on re-entry.
         */
        fun detachAllClients() {
            sessions.values.forEach { it.updateTerminalSessionClient(NoOpTerminalSessionClient) }
        }

        fun terminateSession(id: String) = terminate(listOf(id))

        fun terminateAllSessions() = terminate(sessions.keys.toList())

        fun setWakeLock(acquire: Boolean) {
            if (acquire) {
                if (wakeLock == null) {
                    val pm = getSystemService(POWER_SERVICE) as PowerManager
                    wakeLock = pm.newWakeLock(
                        PowerManager.PARTIAL_WAKE_LOCK,
                        "droidspaces:terminal"
                    ).also { it.acquire() }
                    // Request battery optimization exemption so Android doesn't
                    // kill the service while the wakelock is held.
                    if (!pm.isIgnoringBatteryOptimizations(packageName)) {
                        try {
                            // Direct per-app exemption dialog (requires the manifest permission).
                            startActivity(
                                Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS).apply {
                                    data = Uri.parse("package:$packageName")
                                    flags = Intent.FLAG_ACTIVITY_NEW_TASK
                                }
                            )
                        } catch (e: Exception) {
                            // Fallback: open the general battery optimization settings screen.
                            // Covers OEMs that block the direct intent (some Xiaomi/MIUI builds).
                            try {
                                startActivity(
                                    Intent(Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS).apply {
                                        flags = Intent.FLAG_ACTIVITY_NEW_TASK
                                    }
                                )
                            } catch (e2: Exception) {
                                e2.printStackTrace()
                            }
                        }
                    }
                }
            } else {
                wakeLock?.release()
                wakeLock = null
            }
            updateNotification()
        }

        fun isWakeLockHeld(): Boolean = wakeLock?.isHeld == true
    }

    private val binder = SessionBinder()
    private val notificationManager by lazy { getSystemService(NotificationManager::class.java) }

    override fun onBind(intent: Intent?): IBinder = binder

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
    }

    private fun promoteToForeground() {
        val notification = buildNotification()
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
                startForeground(NOTIFICATION_ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE)
            } else {
                startForeground(NOTIFICATION_ID, notification)
            }
        } catch (se: SecurityException) {
            se.printStackTrace()
            try { startForeground(NOTIFICATION_ID, notification) } catch (e: Exception) { e.printStackTrace() }
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_EXIT -> binder.terminateAllSessions()
            ACTION_WAKE_LOCK_TOGGLE -> binder.setWakeLock(!binder.isWakeLockHeld())
            ACTION_STOP_CONTAINER_SESSIONS -> {
                intent.getStringExtra(EXTRA_CONTAINER_NAME)?.let { terminateSessionsForContainer(it) }
            }
        }
        return START_STICKY
    }

    /** Every tab of a container that stops or restarts. */
    private fun terminateSessionsForContainer(containerName: String) =
        terminate(globalSessionList.filterValues { it.containerName == containerName }.keys.toList())

    /**
     * EOF to each shell first so bash exits and su unwinds on its own; SIGKILL follows
     * 300 ms later for whatever did not. The registry updates at once, so a Restore
     * button never shows for a tab on its way out, but the sessions stay in the map
     * until the kill leg: the service must not stop itself before that, or onDestroy
     * could no longer reach them.
     */
    private fun terminate(ids: Collection<String>) {
        val targets = ids.mapNotNull { id -> sessions[id]?.also { globalSessionList.remove(id) } }
        targets.forEach { runCatching { it.write("\u0004") } }
        updateNotification()
        Handler(Looper.getMainLooper()).postDelayed({
            targets.forEach { runCatching { if (it.emulator != null) it.finishIfRunning() } }
            ids.forEach { sessions.remove(it) }
            updateNotification()
        }, 300)
    }

    override fun onDestroy() {
        sessions.forEach { (_, s) -> s.finishIfRunning() }
        wakeLock?.release()
        wakeLock = null
        globalSessionList.clear()
        super.onDestroy()
    }

    private fun buildNotification(): Notification {
        val openIntent = PendingIntent.getActivity(
            this, 0,
            Intent(this, MainActivity::class.java).apply {
                flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_SINGLE_TOP or Intent.FLAG_ACTIVITY_CLEAR_TOP
            },
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        val exitIntent = PendingIntent.getService(
            this, 1,
            Intent(this, TerminalSessionService::class.java).apply { action = ACTION_EXIT },
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        val wakeLockIntent = PendingIntent.getService(
            this, 2,
            Intent(this, TerminalSessionService::class.java).apply { action = ACTION_WAKE_LOCK_TOGGLE },
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        val wakeLockLabel = if (wakeLock?.isHeld == true) {
            getString(R.string.notification_wakelock_on)
        } else {
            getString(R.string.notification_wakelock_off)
        }
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle(getString(R.string.notification_title))
            .setContentText(notificationText())
            .setSmallIcon(R.drawable.ic_launcher_foreground)
            .setContentIntent(openIntent)
            .addAction(NotificationCompat.Action.Builder(null, wakeLockLabel, wakeLockIntent).build())
            .addAction(NotificationCompat.Action.Builder(null, getString(R.string.notification_action_exit), exitIntent).build())
            .setOngoing(true)
            .build()
    }

    private fun updateNotification() {
        if (sessions.isEmpty()) {
            stopForeground(STOP_FOREGROUND_REMOVE)
            notificationManager.cancel(NOTIFICATION_ID)
            stopSelf()
            return
        }
        notificationManager.notify(NOTIFICATION_ID, buildNotification())
    }

    private fun notificationText(): String {
        val n = sessions.size
        return resources.getQuantityString(R.plurals.notification_sessions_running, n, n)
    }

    private fun createNotificationChannel() {
        val channel = NotificationChannel(
            CHANNEL_ID,
            getString(R.string.notification_channel_name),
            NotificationManager.IMPORTANCE_LOW
        ).apply { description = getString(R.string.notification_channel_desc) }
        notificationManager.createNotificationChannel(channel)
    }

    companion object {
        private const val CHANNEL_ID = "droidspaces_terminal_sessions"
        private const val NOTIFICATION_ID = 42
        private const val ACTION_EXIT = "com.droidspaces.app.TERMINAL_EXIT"
        private const val ACTION_WAKE_LOCK_TOGGLE = "com.droidspaces.app.TERMINAL_WAKE_LOCK_TOGGLE"
        const val ACTION_STOP_CONTAINER_SESSIONS = "com.droidspaces.app.STOP_CONTAINER_SESSIONS"
        const val EXTRA_CONTAINER_NAME = "container_name"

        // Compose-observable process-scoped session registry.
        // Read by any screen that needs live session awareness (e.g. ContainerDetailsScreen).
        val globalSessionList = mutableStateMapOf<String, SessionInfo>()

        fun start(context: Context) {
            context.startService(Intent(context, TerminalSessionService::class.java))
        }

        /** Closes every terminal tab of a container that stopped or is stopping. */
        fun stopContainerSessions(context: Context, containerName: String) {
            context.startService(Intent(context, TerminalSessionService::class.java).apply {
                action = ACTION_STOP_CONTAINER_SESSIONS
                putExtra(EXTRA_CONTAINER_NAME, containerName)
            })
        }
    }
}
