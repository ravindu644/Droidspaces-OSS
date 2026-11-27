package com.droidspaces.app.util

import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent

object TermuxX11Utils {

    /**
     * Brings the Termux:X11 app to the front. The backend already runs the X
     * server inside Termux, so the app only has to show it. Returns false when
     * the app is missing or refuses the intent, so the caller can say so.
     */
    fun launch(context: Context): Boolean {
        val intent = context.packageManager.getLaunchIntentForPackage("com.termux.x11") ?: return false
        return try {
            context.startActivity(intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
            true
        } catch (e: ActivityNotFoundException) {
            false
        } catch (e: SecurityException) {
            false
        }
    }
}
