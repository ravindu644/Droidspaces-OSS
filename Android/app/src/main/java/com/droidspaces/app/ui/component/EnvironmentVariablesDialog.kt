package com.droidspaces.app.ui.component

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.unit.dp
import com.droidspaces.app.R
import com.droidspaces.app.ui.theme.JetBrainsMono
import com.droidspaces.app.util.ValidationUtils

/**
 * The env file itself, one KEY=VALUE per line, with a live count of the
 * lines the backend will apply and a warning for the ones it will skip.
 * The text is handed back as typed, comments included, because
 * parse_env_file_to_config() already skips and logs what it cannot parse.
 */
@Composable
fun EnvironmentVariablesDialog(
    initialContent: String,
    onConfirm: (String) -> Unit,
    onDismiss: () -> Unit,
    confirmLabel: String? = null
) {
    val context = LocalContext.current
    var text by remember { mutableStateOf(initialContent) }

    val entries = text.lines().filter { it.isNotBlank() && !it.trim().startsWith("#") }
    val count = entries.count { ValidationUtils.envLineKey(it) != null }
    val skipped = entries.size - count

    DsDialog(
        onDismiss = onDismiss,
        modifier = Modifier.fillMaxHeight(0.78f).imePadding(),
        scrollableContent = false,
        footer = {
            DialogFooterRow(
                dismissLabel = context.getString(R.string.cancel),
                confirmLabel = confirmLabel ?: context.getString(R.string.ok),
                onDismiss = onDismiss,
                onConfirm = { onConfirm(text.trim()) },
                confirmEnabled = skipped == 0 && text.trim() != initialContent.trim()
            )
        }
    ) {
        Text(
            context.getString(R.string.environment_variables),
            style = MaterialTheme.typography.titleLarge,
            fontWeight = FontWeight.Bold
        )
        OutlinedTextField(
            value = text,
            onValueChange = { text = it },
            modifier = Modifier.weight(1f).fillMaxWidth(),
            // Material sets placeholders to bodyLarge whatever the field's textStyle
            // is, so the hint is given the same style as the typed text explicitly.
            placeholder = {
                Text(
                    context.getString(R.string.env_placeholder),
                    style = MaterialTheme.typography.bodyMedium,
                    fontFamily = JetBrainsMono,
                    color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.4f)
                )
            },
            shape = RoundedCornerShape(16.dp),
            colors = DsTextFieldDefaults.surfaceColors(),
            textStyle = MaterialTheme.typography.bodyMedium.copy(fontFamily = JetBrainsMono),
            keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.None, autoCorrect = false)
        )
        Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text(
                context.getString(R.string.environment_variables_configured, count),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
            )
            if (skipped > 0) {
                Text(
                    context.getString(R.string.env_lines_skipped, skipped),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.error
                )
            }
        }
    }
}
