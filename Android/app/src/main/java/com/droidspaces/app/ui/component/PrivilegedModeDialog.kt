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
                .fillMaxWidth(0.92f)
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

                DangerousWarningCard(
                    title = context.getString(R.string.privileged_warning_title),
                    text = context.getString(R.string.privileged_disclaimer)
                )

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

                // Confirmation Gate (not needed when clearing all flags)
                if (!allOff) {
                    ConfirmPhraseField(
                        value = confirmText,
                        onValueChange = { confirmText = it },
                        isError = confirmText.isNotEmpty() && !isConfirmed
                    )
                }

                DialogFooterRow(
                    dismissLabel = context.getString(R.string.cancel),
                    confirmLabel = context.getString(R.string.ok),
                    onDismiss = onDismiss,
                    onConfirm = {
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
                    // allOff means "clear privileged mode" — a safe action, so it is
                    // enabled without the confirm phrase and uses the primary color.
                    confirmEnabled = isConfirmed || allOff,
                    confirmColor = if (allOff) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.error,
                    confirmContentColor = if (allOff) MaterialTheme.colorScheme.onPrimary else MaterialTheme.colorScheme.onError
                )
            }
        }
    }
}


