package com.droidspaces.app.ui.component

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.droidspaces.app.R

@OptIn(ExperimentalMaterial3Api::class)
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
            // Only untoggle all if they were all toggled (prevent overwriting manual changes)
            // Actually, usually users expect "uncheck full" to "uncheck all"
            nomask = false
            nocaps = false
            noseccomp = false
            shared = false
            unfiltered = false
        }
    }

    // Sync logic: if all individual tags are checked, set 'full' to true
    LaunchedEffect(nomask, nocaps, noseccomp, shared, unfiltered) {
        if (nomask && nocaps && noseccomp && shared && unfiltered) {
            full = true
        }
    }

    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false)
    ) {
        Surface(
            modifier = Modifier
                .fillMaxWidth(0.95f)
                .wrapContentHeight(),
            shape = RoundedCornerShape(28.dp),
            tonalElevation = 6.dp,
            color = MaterialTheme.colorScheme.surface
        ) {
            Column(
                modifier = Modifier
                    .padding(24.dp)
                    .verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(16.dp)
            ) {
                Text(
                    text = context.getString(R.string.privileged_mode),
                    style = MaterialTheme.typography.headlineSmall,
                    fontWeight = FontWeight.Bold
                )

                // Disclaimer Card
                Card(
                    colors = CardDefaults.cardColors(
                        containerColor = MaterialTheme.colorScheme.errorContainer.copy(alpha = 0.7f)
                    ),
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Text(
                            text = context.getString(R.string.privileged_warning_title),
                            style = MaterialTheme.typography.titleMedium,
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

                // Granular Toggles
                PrivilegedToggle(
                    title = "full",
                    description = context.getString(R.string.privileged_full_desc),
                    checked = full,
                    onCheckedChange = { full = it }
                )

                HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))

                PrivilegedToggle(
                    title = "nomask",
                    description = context.getString(R.string.privileged_nomask_desc),
                    checked = nomask,
                    onCheckedChange = { nomask = it },
                    enabled = !full
                )

                PrivilegedToggle(
                    title = "nocaps",
                    description = context.getString(R.string.privileged_nocaps_desc),
                    checked = nocaps,
                    onCheckedChange = { nocaps = it },
                    enabled = !full
                )

                PrivilegedToggle(
                    title = "noseccomp",
                    description = context.getString(R.string.privileged_noseccomp_desc),
                    checked = noseccomp,
                    onCheckedChange = { noseccomp = it },
                    enabled = !full
                )

                PrivilegedToggle(
                    title = "shared",
                    description = context.getString(R.string.privileged_shared_desc),
                    checked = shared,
                    onCheckedChange = { shared = it },
                    enabled = !full
                )

                PrivilegedToggle(
                    title = "unfiltered-dev",
                    description = context.getString(R.string.privileged_unfiltered_desc),
                    checked = unfiltered,
                    onCheckedChange = { unfiltered = it },
                    enabled = !full
                )

                Spacer(modifier = Modifier.height(8.dp))

                // Confirmation Gate
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
                        isError = confirmText.isNotEmpty() && !isConfirmed
                    )
                }

                // Buttons
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.End,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    TextButton(onClick = onDismiss) {
                        Text(context.getString(R.string.cancel))
                    }
                    Spacer(modifier = Modifier.width(8.dp))
                    Button(
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
                        },
                        enabled = isConfirmed,
                        colors = ButtonDefaults.buttonColors(
                            containerColor = MaterialTheme.colorScheme.error
                        )
                    ) {
                        Text(context.getString(R.string.ok))
                    }
                }
            }
        }
    }
}

@Composable
private fun PrivilegedToggle(
    title: String,
    description: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit,
    enabled: Boolean = true
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = title,
                style = MaterialTheme.typography.bodyLarge,
                fontWeight = FontWeight.SemiBold,
                color = if (enabled) MaterialTheme.colorScheme.onSurface else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f)
            )
            Text(
                text = description,
                style = MaterialTheme.typography.bodySmall,
                color = if (enabled) MaterialTheme.colorScheme.onSurfaceVariant else MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.38f)
            )
        }
        Checkbox(
            checked = checked,
            onCheckedChange = onCheckedChange,
            enabled = enabled
        )
    }
}
