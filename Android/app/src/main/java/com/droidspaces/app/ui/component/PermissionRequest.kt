package com.droidspaces.app.ui.component

import android.app.Activity
import android.content.Intent
import android.content.pm.PackageManager
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.core.content.ContextCompat
import com.droidspaces.app.R

/**
 * Asks for [permission] and, when the user says no, explains why. Allow asks again while Android
 * still shows the prompt. After "don't ask again" the prompt returns denied without appearing, so
 * Allow opens [settingsIntent] instead. Call the returned function with a callback that gets the
 * outcome: true once granted, false when the user walks away or is sent to Settings.
 */
@Composable
fun rememberPermissionRequest(
    permission: String,
    title: String,
    rationale: String,
    settingsIntent: Intent,
): ((Boolean) -> Unit) -> Unit {
    val context = LocalContext.current
    val activity = context as? Activity
    var showRationale by remember { mutableStateOf(false) }
    var onResult by remember { mutableStateOf<(Boolean) -> Unit>({}) }
    val launcher = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
        if (granted) onResult(true) else showRationale = true
    }

    if (showRationale) {
        val dismiss = {
            showRationale = false
            onResult(false)
        }
        DsDialog(
            onDismiss = dismiss,
            footer = {
                DialogFooterRow(
                    dismissLabel = context.getString(R.string.not_now),
                    confirmLabel = context.getString(R.string.allow),
                    onDismiss = dismiss,
                    onConfirm = {
                        showRationale = false
                        if (activity?.shouldShowRequestPermissionRationale(permission) == true) {
                            launcher.launch(permission)
                        } else {
                            context.startActivity(settingsIntent)
                            onResult(false)
                        }
                    }
                )
            }
        ) {
            Text(text = title, style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold)
            Text(
                text = rationale,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }

    return { callback ->
        onResult = callback
        when {
            ContextCompat.checkSelfPermission(context, permission) == PackageManager.PERMISSION_GRANTED -> callback(true)
            activity?.shouldShowRequestPermissionRationale(permission) == true -> showRationale = true
            else -> launcher.launch(permission)
        }
    }
}
