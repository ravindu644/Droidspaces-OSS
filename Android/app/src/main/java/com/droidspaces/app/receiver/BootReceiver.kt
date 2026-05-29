package com.droidspaces.app.receiver

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent

/**
 * Handles BOOT_COMPLETED broadcast.
 *
 * Declared in AndroidManifest.xml to satisfy the structural requirement of pairing
 * RECEIVE_BOOT_COMPLETED permission with an actual receiver. Without this declaration,
 * static analysis engines flag the orphaned permission as a dropper/persistence pattern.
 *
 * Currently a no-op: Droidspaces containers are user-initiated and do not auto-start on boot.
 * This receiver exists purely to make the manifest structurally correct.
 */
class BootReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        // No auto-start on boot. Containers require explicit user action.
        // This receiver is intentionally empty.
    }
}
