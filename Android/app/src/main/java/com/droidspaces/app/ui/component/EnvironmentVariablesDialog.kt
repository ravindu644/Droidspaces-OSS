package com.droidspaces.app.ui.component

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import com.droidspaces.app.ui.theme.JetBrainsMono
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.droidspaces.app.R

data class EnvVar(
    val id: String = java.util.UUID.randomUUID().toString(),
    val key: String = "",
    val value: String = ""
)

/** Parses "KEY=VALUE\n..." format into a list of EnvVar pairs with stable IDs. */
private fun parseEnvContent(content: String): List<EnvVar> =
    content.lines()
        .map { it.trim() }
        .filter { it.isNotEmpty() && !it.startsWith("#") && it.contains("=") }
        .map { line ->
            val idx = line.indexOf('=')
            EnvVar(key = line.substring(0, idx).trim(), value = line.substring(idx + 1).trim())
        }

/** Serializes back to "KEY=VALUE\n..." format. */
private fun serializeEnvVars(vars: List<EnvVar>): String =
    vars.filter { it.key.isNotBlank() }
        .joinToString("\n") { "${it.key}=${it.value}" }

@Composable
fun EnvironmentVariablesDialog(
    initialContent: String,
    onConfirm: (String) -> Unit,
    onDismiss: () -> Unit,
    confirmLabel: String? = null
) {
    val context = LocalContext.current
    var vars by remember {
        mutableStateOf(
            parseEnvContent(initialContent).ifEmpty { listOf(EnvVar(key = "", value = "")) }
        )
    }

    val fieldShape = RoundedCornerShape(14.dp)
    val fieldColors = OutlinedTextFieldDefaults.colors(
        unfocusedBorderColor = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f),
        focusedBorderColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.8f),
        unfocusedContainerColor = MaterialTheme.colorScheme.surface.copy(alpha = 0.5f),
        focusedContainerColor = MaterialTheme.colorScheme.surface.copy(alpha = 0.7f)
    )

    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false)
    ) {
        Surface(
            modifier = Modifier
                .fillMaxWidth(0.92f)
                .fillMaxHeight(0.78f),
            shape = RoundedCornerShape(24.dp),
            color = MaterialTheme.colorScheme.surfaceContainer,
            border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f)),
            tonalElevation = 0.dp
        ) {
            Column(modifier = Modifier.fillMaxSize().padding(24.dp)) {
                // Header
                Text(
                    context.getString(R.string.environment_variables),
                    style = MaterialTheme.typography.titleLarge,
                    fontWeight = FontWeight.Bold
                )
                Text(
                    "Each row is one KEY=VALUE pair",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f),
                    modifier = Modifier.padding(top = 2.dp)
                )

                Spacer(modifier = Modifier.height(16.dp))

                // Column headers
                Row(
                    modifier = Modifier.fillMaxWidth().padding(horizontal = 4.dp, vertical = 4.dp),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Text(
                        "KEY",
                        style = MaterialTheme.typography.labelSmall,
                        fontFamily = JetBrainsMono,
                        color = MaterialTheme.colorScheme.primary,
                        modifier = Modifier.weight(1f)
                    )
                    Text(
                        "VALUE",
                        style = MaterialTheme.typography.labelSmall,
                        fontFamily = JetBrainsMono,
                        color = MaterialTheme.colorScheme.primary,
                        modifier = Modifier.weight(1f)
                    )
                    Spacer(modifier = Modifier.width(40.dp)) // delete btn space
                }

                HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.3f))

                Spacer(modifier = Modifier.height(8.dp))

                // Env var list
                LazyColumn(
                    modifier = Modifier.weight(1f).fillMaxWidth(),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    itemsIndexed(vars, key = { _, item -> item.id }) { index, envVar ->
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.spacedBy(8.dp),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            OutlinedTextField(
                                value = envVar.key,
                                onValueChange = { new ->
                                    vars = vars.toMutableList().also { it[index] = envVar.copy(key = new) }
                                },
                                placeholder = {
                                    Text("KEY", fontFamily = JetBrainsMono, fontSize = 12.sp, color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.4f))
                                },
                                modifier = Modifier.weight(1f),
                                singleLine = true,
                                shape = fieldShape,
                                colors = fieldColors,
                                textStyle = MaterialTheme.typography.bodyMedium.copy(fontFamily = JetBrainsMono, fontSize = 13.sp)
                            )
                            OutlinedTextField(
                                value = envVar.value,
                                onValueChange = { new ->
                                    vars = vars.toMutableList().also { it[index] = envVar.copy(value = new) }
                                },
                                placeholder = {
                                    Text("value", fontFamily = JetBrainsMono, fontSize = 12.sp, color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.4f))
                                },
                                modifier = Modifier.weight(1f),
                                singleLine = true,
                                shape = fieldShape,
                                colors = fieldColors,
                                textStyle = MaterialTheme.typography.bodyMedium.copy(fontFamily = JetBrainsMono, fontSize = 13.sp)
                            )

                            // Only show delete button for index > 0 (Added rows)
                            if (index > 0) {
                                IconButton(
                                    onClick = { vars = vars.toMutableList().also { it.removeAt(index) } },
                                    modifier = Modifier.size(36.dp)
                                ) {
                                    Icon(
                                        Icons.Default.Delete, contentDescription = null,
                                        modifier = Modifier.size(18.dp),
                                        tint = MaterialTheme.colorScheme.error.copy(alpha = 0.7f)
                                    )
                                }
                            } else {
                                // Keep layout consistent for Row 0
                                Spacer(modifier = Modifier.width(36.dp))
                            }
                        }
                    }
                }

                Spacer(modifier = Modifier.height(12.dp))

                // Add row button
                val addBtnShape = RoundedCornerShape(14.dp)
                Surface(
                    modifier = Modifier.fillMaxWidth().clip(addBtnShape).clickable(
                        onClick = { vars = vars + EnvVar() }
                    ),
                    shape = addBtnShape,
                    color = MaterialTheme.colorScheme.surfaceContainerHigh,
                    border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.3f)),
                    tonalElevation = 0.dp
                ) {
                    Row(
                        modifier = Modifier.padding(12.dp),
                        horizontalArrangement = Arrangement.Center,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Icon(Icons.Default.Add, contentDescription = null, modifier = Modifier.size(16.dp), tint = MaterialTheme.colorScheme.onSurfaceVariant)
                        Spacer(modifier = Modifier.width(6.dp))
                        Text("Add variable", style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                }

                Spacer(modifier = Modifier.height(16.dp))

                // Action buttons
                Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                    Surface(
                        modifier = Modifier.weight(1f).clip(RoundedCornerShape(14.dp)).clickable(onClick = onDismiss),
                        shape = RoundedCornerShape(14.dp),
                        color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.06f),
                        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f)),
                        tonalElevation = 0.dp
                    ) {
                        Box(modifier = Modifier.padding(14.dp), contentAlignment = Alignment.Center) {
                            Text(context.getString(R.string.cancel), style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.SemiBold)
                        }
                    }
                    Surface(
                        modifier = Modifier.weight(1f).clip(RoundedCornerShape(14.dp)).clickable(
                            onClick = { onConfirm(serializeEnvVars(vars)) }
                        ),
                        shape = RoundedCornerShape(14.dp),
                        color = MaterialTheme.colorScheme.primary,
                        tonalElevation = 0.dp
                    ) {
                        Box(modifier = Modifier.padding(14.dp), contentAlignment = Alignment.Center) {
                            Text(
                                confirmLabel ?: context.getString(R.string.ok),
                                style = MaterialTheme.typography.labelLarge,
                                fontWeight = FontWeight.SemiBold,
                                color = MaterialTheme.colorScheme.onPrimary
                            )
                        }
                    }
                }
            }
        }
    }
}
