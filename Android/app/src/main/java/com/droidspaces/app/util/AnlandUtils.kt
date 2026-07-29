package com.droidspaces.app.util

import android.content.ActivityNotFoundException
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.widget.Toast
import com.droidspaces.app.R

/**
 * Helpers for the anland display integration on the app side.
 */
object AnlandUtils {

    private const val CONSUMER_PACKAGE = "com.anland.consumer"
    private const val CONSUMER_ACTIVITY = "com.anland.consumer.SecondaryActivity"
    // Matches MainActivity.EXTRA_SOCKET_PATH / EXTRA_WINDOW_NAME in the consumer app.
    private const val EXTRA_SOCKET_PATH = "socket_path"
    private const val EXTRA_WINDOW_NAME = "window_name"

    /**
     * Launch the anland consumer app's multi-window activity, pointed at this
     * container's display socket and titled with the container name. The consumer
     * dedups by socket path, so re-launching focuses the existing window. Shows a
     * toast and does nothing if the consumer app isn't installed.
     */
    fun launchWindow(context: Context, containerName: String, socketPath: String) {
        val intent = Intent(Intent.ACTION_MAIN).apply {
            component = ComponentName(CONSUMER_PACKAGE, CONSUMER_ACTIVITY)
            putExtra(EXTRA_SOCKET_PATH, socketPath)
            putExtra(EXTRA_WINDOW_NAME, containerName)
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_MULTIPLE_TASK)
        }
        try {
            context.startActivity(intent)
        } catch (e: ActivityNotFoundException) {
            Toast.makeText(
                context,
                context.getString(R.string.anland_not_installed),
                Toast.LENGTH_SHORT
            ).show()
        }
    }
}
