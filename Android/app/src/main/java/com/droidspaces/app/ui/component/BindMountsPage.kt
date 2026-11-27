package com.droidspaces.app.ui.component

import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Folder
import androidx.compose.material.icons.filled.FolderOpen
import androidx.compose.material.icons.filled.Lock
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.foundation.layout.Column
import androidx.compose.runtime.key
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import com.droidspaces.app.R
import com.droidspaces.app.ui.theme.JetBrainsMono
import com.droidspaces.app.util.BindMount
import com.droidspaces.app.util.ValidationUtils

/**
 * The bind mounts as a draft list, laid out like the network page's lists:
 * the entries in a group, the add button under it, a sheet to add one. Every
 * entry in the list is already valid, so leaving always keeps the draft.
 */
@Composable
fun BindMountsPage(mounts: List<BindMount>, onSave: (List<BindMount>) -> Unit, onClose: () -> Unit) {
    val context = LocalContext.current
    var draft by remember { mutableStateOf(mounts) }
    var adding by rememberSaveable { mutableStateOf(false) }

    DraftPageScaffold(
        title = context.getString(R.string.bind_mounts),
        dirty = draft != mounts,
        canSave = true,
        onSave = { onSave(draft) },
        onClose = onClose
    ) {
        GroupHeader(context.getString(R.string.bind_mounts), sub = true)
        GroupIntro(context.getString(R.string.bind_mounts_intro))
        EntryGroup(visible = draft.isNotEmpty()) {
            draft.forEachIndexed { i, mount ->
                key(mount) {
                    AnimatedRemoval(onRemoved = { draft = draft - mount }) { remove ->
                        Column {
                            if (i > 0) GroupDivider()
                            EntryRow(
                                text = mount.dest,
                                supporting = context.getString(R.string.mount_from, mount.src),
                                deleteDescription = context.getString(R.string.delete_item, mount.dest),
                                onDelete = remove,
                                leading = { Icon(Icons.Default.Folder, contentDescription = null, tint = MaterialTheme.colorScheme.primary) },
                                trailingBadge = if (mount.ro) { { ReadOnlyBadge() } } else null
                            )
                        }
                    }
                }
            }
        }
        GroupAddButton(context.getString(R.string.add_bind_mount), count = draft.size) { adding = true }
    }

    if (adding) {
        AddBindMountSheet(
            existing = draft,
            onDismiss = { adding = false },
            onAdd = { draft = draft + it; adding = false }
        )
    }
}

/**
 * One new bind mount: a folder on the phone from the file picker, where it
 * shows up in the container, and whether the container may write to it.
 * Add stays off until both paths would save as typed and the destination is
 * not taken.
 */
@Composable
private fun AddBindMountSheet(existing: List<BindMount>, onDismiss: () -> Unit, onAdd: (BindMount) -> Unit) {
    val context = LocalContext.current
    var src by rememberSaveable { mutableStateOf("") }
    var dest by rememberSaveable { mutableStateOf("") }
    var ro by rememberSaveable { mutableStateOf(false) }
    var picking by rememberSaveable { mutableStateOf(false) }
    val destOk = ValidationUtils.isValidBindDestination(dest)
    // The folder picker can return any path, so the source needs the same separator check.
    val srcOk = ValidationUtils.isBindSafe(src)
    val taken = existing.any { it.dest == dest }
    val valid = src.isNotEmpty() && srcOk && destOk && !taken

    DsBottomSheet(
        onDismiss = onDismiss,
        title = context.getString(R.string.add_bind_mount),
        footer = {
            DialogFooterRow(
                dismissLabel = context.getString(R.string.cancel),
                confirmLabel = context.getString(R.string.add),
                onDismiss = { close(onDismiss) },
                onConfirm = { if (valid) close { onAdd(BindMount(src, dest, ro)) } },
                confirmEnabled = valid
            )
        }
    ) {
        Surface(shape = RoundedCornerShape(20.dp), color = MaterialTheme.colorScheme.surfaceContainerHigh) {
            NavRow(
                icon = Icons.Default.FolderOpen,
                title = context.getString(R.string.folder_on_phone),
                value = src.ifEmpty { context.getString(R.string.choose_folder) },
                valueFontFamily = if (src.isEmpty()) null else JetBrainsMono,
                error = if (srcOk) null else context.getString(R.string.bind_path_separator),
                onClick = { picking = true }
            )
        }
        MonoField(
            value = dest,
            onValueChange = { v -> dest = v.filter { !it.isWhitespace() } },
            label = context.getString(R.string.path_in_container),
            placeholder = context.getString(R.string.container_path_placeholder),
            supporting = context.getString(if (taken) R.string.bind_dest_taken else R.string.path_in_container_hint),
            isError = dest.isNotEmpty() && (!destOk || taken),
            keyboardType = KeyboardType.Uri
        )
        Surface(shape = RoundedCornerShape(20.dp), color = MaterialTheme.colorScheme.surfaceContainerHigh) {
            SwitchItem(
                icon = Icons.Default.Lock,
                title = context.getString(R.string.read_only),
                summary = context.getString(R.string.read_only_description),
                checked = ro,
                onCheckedChange = { ro = it }
            )
        }
    }

    if (picking) {
        FilePickerDialog(
            onDismiss = { picking = false },
            onConfirm = { src = it; picking = false },
            // Bind mounting the host root hands the container the whole host filesystem.
            allowRoot = false
        )
    }
}

@Composable
private fun ReadOnlyBadge() {
    Surface(
        shape = RoundedCornerShape(8.dp),
        color = MaterialTheme.colorScheme.secondaryContainer,
        modifier = Modifier.padding(start = 8.dp)
    ) {
        Text(
            LocalContext.current.getString(R.string.read_only),
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSecondaryContainer,
            maxLines = 1,
            modifier = Modifier.padding(horizontal = 6.dp, vertical = 2.dp)
        )
    }
}
