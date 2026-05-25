package com.droidspaces.app.ui.component

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.droidspaces.app.ui.component.ToggleCard
import com.droidspaces.app.R

@Composable
fun PrivilegedModeDialog(
    initialPrivileged: String,
    onConfirm: (String) -> Unit,
    onDismiss: () -> Unit
) {
    val context = LocalContext.current
    
    // Parse initial state
    val initialTags = initialPrivileged.split(",").filter { it.isNotEmpty() }.toSet()
    
    var nomask by remember { mutableStateOf(initialTags.contains("nomask")) }
    var nocaps by remember { mutableStateOf(initialTags.contains("nocaps")) }
    var noseccomp by remember { mutableStateOf(initialTags.contains("noseccomp")) }
    var shared by remember { mutableStateOf(initialTags.contains("shared")) }
    var unfiltered by remember { mutableStateOf(initialTags.contains("unfiltered-dev")) }
    var full by remember { mutableStateOf(initialTags.contains("full")) }
    
    var confirmText by remember { mutableStateOf("") }
    val isConfirmed = confirmText == context.getString(R.string.i_understand_caps)

    // Sync logic for 'full' mode
    LaunchedEffect(full) {
        if (full) {
            nomask = true
            nocaps = true
            noseccomp = true
            shared = true
            unfiltered = true
        } else if (nomask && nocaps && noseccomp && shared && unfiltered) {
            // If full was toggled off while all children were on, toggle them all off
            nomask = false
            nocaps = false
            noseccomp = false
            shared = false
            unfiltered = false
        }
    }

    // Sync logic: if any individual tag is manually unchecked, 'full' must be false
    LaunchedEffect(nomask, nocaps, noseccomp, shared, unfiltered) {
        if (!nomask || !nocaps || !noseccomp || !shared || !unfiltered) {
            full = false
        } else {
            full = true
        }
    }

    val allOff = !nomask && !nocaps && !noseccomp && !shared && !unfiltered && !full

    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false)
    ) {
        Surface(
            modifier = Modifier
                .fillMaxWidth(0.95f)
                .wrapContentHeight(),
            shape = RoundedCornerShape(24.dp),
            color = MaterialTheme.colorScheme.surfaceContainer,
            border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f)),
            tonalElevation = 0.dp
        ) {
            Column(
                modifier = Modifier
                    .padding(24.dp)
                    .verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(16.dp)
            ) {
                Text(
                    text = context.getString(R.string.privileged_mode),
                    style = MaterialTheme.typography.titleLarge,
                    fontWeight = FontWeight.Bold
                )

                // Disclaimer Card
                Surface(
                    color = MaterialTheme.colorScheme.errorContainer.copy(alpha = 0.3f),
                    shape = RoundedCornerShape(16.dp),
                    border = BorderStroke(1.dp, MaterialTheme.colorScheme.error.copy(alpha = 0.2f)),
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Text(
                            text = context.getString(R.string.privileged_warning_title),
                            style = MaterialTheme.typography.titleSmall,
                            fontWeight = FontWeight.Bold,
                            color = MaterialTheme.colorScheme.error
                        )
                        Spacer(modifier = Modifier.height(4.dp))
                        Text(
                            text = context.getString(R.string.privileged_disclaimer),
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onErrorContainer
                        )
                    }
                }

                // Granular Toggles using modern look
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    ToggleCard(
                        title = "full",
                        description = context.getString(R.string.privileged_full_desc),
                        checked = full,
                        onCheckedChange = { full = it }
                    )

                    HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp), color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f))

                    ToggleCard(
                        title = "nomask",
                        description = context.getString(R.string.privileged_nomask_desc),
                        checked = nomask,
                        onCheckedChange = { nomask = it },
                        enabled = !full
                    )

                    ToggleCard(
                        title = "nocaps",
                        description = context.getString(R.string.privileged_nocaps_desc),
                        checked = nocaps,
                        onCheckedChange = { nocaps = it },
                        enabled = !full
                    )

                    ToggleCard(
                        title = "noseccomp",
                        description = context.getString(R.string.privileged_noseccomp_desc),
                        checked = noseccomp,
                        onCheckedChange = { noseccomp = it },
                        enabled = !full
                    )

                    ToggleCard(
                        title = "shared",
                        description = context.getString(R.string.privileged_shared_desc),
                        checked = shared,
                        onCheckedChange = { shared = it },
                        enabled = !full
                    )

                    ToggleCard(
                        title = "unfiltered-dev",
                        description = context.getString(R.string.privileged_unfiltered_desc),
                        checked = unfiltered,
                        onCheckedChange = { unfiltered = it },
                        enabled = !full
                    )
                }

                // Confirmation Gate
                if (!allOff) {
                    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                        Text(
                            text = context.getString(R.string.privileged_confirm_instruction),
                            style = MaterialTheme.typography.labelMedium,
                            fontWeight = FontWeight.Bold
                        )
                        OutlinedTextField(
                            value = confirmText,
                            onValueChange = { confirmText = it },
                            modifier = Modifier.fillMaxWidth(),
                            placeholder = { Text(context.getString(R.string.i_understand_caps)) },
                            singleLine = true,
                            isError = confirmText.isNotEmpty() && !isConfirmed,
                            shape = RoundedCornerShape(14.dp),
                            colors = OutlinedTextFieldDefaults.colors(
                                unfocusedBorderColor = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f),
                                focusedBorderColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.8f),
                                unfocusedContainerColor = MaterialTheme.colorScheme.surface.copy(alpha = 0.5f),
                                focusedContainerColor = MaterialTheme.colorScheme.surface.copy(alpha = 0.7f)
                            )
                        )
                    }
                }

                // Buttons
                Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                    Surface(
                        modifier = Modifier.weight(1f).clip(RoundedCornerShape(14.dp)).clickable(onClick = onDismiss),
                        shape = RoundedCornerShape(14.dp),
                        color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.06f),
                        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.4f)),
                        tonalElevation = 0.dp
                    ) {
                        Box(modifier = Modifier.padding(14.dp), contentAlignment = Alignment.Center) {
                            Text(context.getString(R.string.cancel), style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.SemiBold)
                        }
                    }
                    Surface(
                        modifier = Modifier.weight(1f).clip(RoundedCornerShape(14.dp)).clickable(
                            enabled = isConfirmed || allOff,
                            onClick = {
                                val tags = mutableListOf<String>()
                                if (full) {
                                    tags.add("full")
                                } else {
                                    if (nomask) tags.add("nomask")
                                    if (nocaps) tags.add("nocaps")
                                    if (noseccomp) tags.add("noseccomp")
                                    if (shared) tags.add("shared")
                                    if (unfiltered) tags.add("unfiltered-dev")
                                }
                                onConfirm(tags.joinToString(","))
                            }
                        ),
                        shape = RoundedCornerShape(14.dp),
                        color = if (isConfirmed || allOff) {
                            if (allOff) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.error
                        } else {
                            MaterialTheme.colorScheme.onSurface.copy(alpha = 0.12f)
                        },
                        tonalElevation = 0.dp
                    ) {
                        Box(modifier = Modifier.padding(14.dp), contentAlignment = Alignment.Center) {
                            Text(
                                context.getString(R.string.ok),
                                style = MaterialTheme.typography.labelLarge,
                                fontWeight = FontWeight.SemiBold,
                                color = if (isConfirmed || allOff) {
                                    if (allOff) MaterialTheme.colorScheme.onPrimary else MaterialTheme.colorScheme.onError
                                } else {
                                    MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f)
                                }
                            )
                        }
                    }
                }
            }
        }
    }
}


