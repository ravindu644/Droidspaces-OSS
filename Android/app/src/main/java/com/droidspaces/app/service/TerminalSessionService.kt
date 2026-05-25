package com.droidspaces.app.service

import android.app.*
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Binder
import android.os.Build
import android.os.IBinder
import android.os.PowerManager
import androidx.compose.runtime.mutableStateMapOf
import androidx.core.app.NotificationCompat
import com.droidspaces.app.MainActivity
import com.droidspaces.app.R
import com.droidspaces.app.ui.terminal.DroidspacesTerminalSession
import com.termux.terminal.TerminalSession
import com.termux.terminal.TerminalSessionClient

data class SessionInfo(val containerName: String, val user: String, val fontSizePx: Int = 30)

class TerminalSessionService : Service() {

    private val sessions = hashMapOf<String, TerminalSession>()
    val sessionList = mutableStateMapOf<String, SessionInfo>()

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
                val info = SessionInfo(containerName, user)
                sessionList[sessionId] = info
                globalSessionList[sessionId] = info
                if (sessions.size == 1) {
                    promoteToForeground()
                } else {
                    updateNotification()
                }
            }
        }

        fun getSession(id: String): TerminalSession? = sessions[id]

        fun terminateSession(id: String) {
            runCatching {
                // Send Ctrl+D (EOF) first so bash exits cleanly and su unwinds.
                // SIGKILL fires 300 ms later as a safety net for anything that
                // didn't respond in time.
                sessions[id]?.write("\u0004")

                // Update UI state IMMEDIATELY to prevent "Restore" button glitches.
                sessionList.remove(id)
                globalSessionList.remove(id)
                updateNotification()

                android.os.Handler(android.os.Looper.getMainLooper()).postDelayed({
                    runCatching {
                        sessions[id]?.apply { if (emulator != null) finishIfRunning() }
                        sessions.remove(id)
                        if (sessions.isEmpty()) {
                            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                                stopForeground(STOP_FOREGROUND_REMOVE)
                            } else {
                                @Suppress("DEPRECATION")
                                stopForeground(true)
                            }
                            stopSelf()
                        } else {
                            updateNotification()
                        }
                    }.onFailure { it.printStackTrace() }
                }, 300)
            }.onFailure { it.printStackTrace() }
        }

        fun terminateAllSessions() {
            // EOF to all shells first - cleans up every su -> bash chain gracefully.
            sessions.values.forEach { it.write("\u0004") }

            // Clear UI state IMMEDIATELY.
            sessionList.clear()
            globalSessionList.clear()
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                stopForeground(STOP_FOREGROUND_REMOVE)
            } else {
                @Suppress("DEPRECATION")
                stopForeground(true)
            }

            android.os.Handler(android.os.Looper.getMainLooper()).postDelayed({
                sessions.values.forEach { it.finishIfRunning() }
                sessions.clear()
                stopSelf()
            }, 300)
        }

        fun setWakeLock(acquire: Boolean) {
            if (acquire) {
                if (wakeLock == null) {
                    val pm = getSystemService(POWER_SERVICE) as PowerManager
                    wakeLock = pm.newWakeLock(
                        PowerManager.PARTIAL_WAKE_LOCK,
                        "droidspaces:terminal"
                    ).also { it.acquire() }
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
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) createNotificationChannel()
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
            ACTION_EXIT -> {
                binder.terminateAllSessions()
            }
            ACTION_WAKE_LOCK_TOGGLE -> binder.setWakeLock(!binder.isWakeLockHeld())
        }
        return START_STICKY
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
        notificationManager.notify(NOTIFICATION_ID, buildNotification())
    }

    private fun notificationText(): String {
        val n = sessions.size
        return resources.getQuantityString(R.plurals.notification_sessions_running, n, n)
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                getString(R.string.notification_channel_name),
                NotificationManager.IMPORTANCE_LOW
            ).apply { description = getString(R.string.notification_channel_desc) }
            notificationManager.createNotificationChannel(channel)
        }
    }

    companion object {
        private const val CHANNEL_ID = "droidspaces_terminal_sessions"
        private const val NOTIFICATION_ID = 42
        private const val ACTION_EXIT = "com.droidspaces.app.TERMINAL_EXIT"
        private const val ACTION_WAKE_LOCK_TOGGLE = "com.droidspaces.app.TERMINAL_WAKE_LOCK_TOGGLE"

        // Compose-observable process-scoped session registry.
        // Read by any screen that needs live session awareness (e.g. ContainerDetailsScreen).
        val globalSessionList = mutableStateMapOf<String, SessionInfo>()

        fun start(context: Context) {
            context.startService(Intent(context, TerminalSessionService::class.java))
        }
    }
}
