package com.droidspaces.app.ui.component

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import com.droidspaces.app.R

/**
 * Asked before exporting a container that has environment variables. They often hold
 * passwords and tokens and exports get shared, so the archive leaves them out unless
 * the user turns this on.
 */
@Composable
fun ExportContainerDialog(
    containerName: String,
    envCount: Int,
    onConfirm: (includeEnv: Boolean) -> Unit,
    onDismiss: () -> Unit
) {
    var includeEnv by remember { mutableStateOf(false) }

    DsDialog(
        onDismiss = onDismiss,
        footer = {
            DialogFooterRow(
                dismissLabel = stringResource(R.string.cancel),
                confirmLabel = stringResource(R.string.export_action),
                onDismiss = onDismiss,
                onConfirm = { onConfirm(includeEnv) }
            )
        }
    ) {
        Text(
            text = stringResource(R.string.export_container_title, containerName),
            style = MaterialTheme.typography.titleLarge,
            fontWeight = FontWeight.Bold
        )
        Text(
            text = pluralStringResource(R.plurals.export_env_body, envCount, envCount),
            style = MaterialTheme.typography.bodyMedium
        )
        ToggleCard(
            title = stringResource(R.string.export_env_include),
            description = stringResource(R.string.export_env_include_desc),
            checked = includeEnv,
            onCheckedChange = { includeEnv = it }
        )
    }
}
