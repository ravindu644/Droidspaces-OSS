package com.droidspaces.app.ui.component

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RadioButton
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import com.droidspaces.app.R
import com.droidspaces.app.ui.theme.JetBrainsMono
import com.droidspaces.app.ui.util.FocusUtils
import com.droidspaces.app.util.ContainerManager
import com.droidspaces.app.util.PortForward
import com.droidspaces.app.util.ValidationUtils

/**
 * Pick upstream interfaces in the order they should be tried: each tap numbers
 * the interface, a second tap takes it out and closes the gap. The list opens
 * with what is already set, ticked in its order, so the sheet edits the whole
 * list rather than only adding to it; names already set but not up right now,
 * a wildcard for instance, are listed too. A typed name, wildcards allowed,
 * goes after the picked ones. [max] caps the list.
 */
@Composable
fun UpstreamPickerSheet(
    current: List<String>,
    max: Int,
    onDismiss: () -> Unit,
    onDone: (List<String>) -> Unit,
) {
    val context = LocalContext.current
    var live by remember { mutableStateOf<List<String>?>(null) }
    LaunchedEffect(Unit) { live = ContainerManager.listUpstreamInterfaces() }
    val picked = remember { mutableStateListOf<String>().apply { addAll(current) } }
    var typed by rememberSaveable { mutableStateOf("") }

    val result = (picked + listOfNotNull(typed.trim().takeIf { it.isNotEmpty() })).distinct()
    val canSave = result != current && result.size <= max

    DsBottomSheet(
        onDismiss = onDismiss,
        title = context.getString(R.string.add_upstream_interface),
        titleAction = { DsRefreshButton(context.getString(R.string.refresh), { live = ContainerManager.listUpstreamInterfaces() }) },
        footer = {
            DialogFooterRow(
                dismissLabel = context.getString(R.string.cancel),
                confirmLabel = context.getString(R.string.ok),
                onDismiss = { close(onDismiss) },
                onConfirm = { close { onDone(result) } },
                confirmEnabled = canSave
            )
        }
    ) {
        Text(context.getString(R.string.add_interfaces_lead), style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
        PickList(live?.let { (current + it).distinct() }) { iface ->
            val order = picked.indexOf(iface)
            OptionRow(
                name = iface,
                modifier = Modifier.toggleable(value = order >= 0, role = Role.Checkbox) { on ->
                    if (on) { if (picked.size < max) picked.add(iface) } else picked.remove(iface)
                }
            ) { OrderBox(if (order >= 0) order + 1 else null) }
        }
        val full = picked.size >= max
        MonoField(
            value = typed,
            onValueChange = { typed = ValidationUtils.ifaceNameInput(it, wildcards = true) },
            label = context.getString(R.string.or_type_a_name),
            placeholder = "v4-rmnet_data*",
            // At the cap, a tap on another row or a typed name cannot fit; say so
            // rather than leave OK grey for no visible reason.
            supporting = if (full) context.getString(R.string.upstream_max, max) else context.getString(R.string.add_interfaces_manual_hint),
            isError = result.size > max
        )
    }
}

/**
 * Pick the one host interface a macvlan sits on, or type its exact name. A
 * typed name keeps only interface-name characters, because it goes into the
 * config file as a key=value line.
 */
@Composable
fun HostInterfaceSheet(onDismiss: () -> Unit, onPick: (String) -> Unit) {
    val context = LocalContext.current
    var available by remember { mutableStateOf<List<String>?>(null) }
    LaunchedEffect(Unit) { available = ContainerManager.listEthernetInterfaces() }
    var selected by rememberSaveable { mutableStateOf("") }
    var typed by rememberSaveable { mutableStateOf("") }
    val choice = typed.ifBlank { selected }

    DsBottomSheet(
        onDismiss = onDismiss,
        title = context.getString(R.string.macvlan_parent),
        titleAction = { DsRefreshButton(context.getString(R.string.refresh), { available = ContainerManager.listEthernetInterfaces() }) },
        footer = {
            DialogFooterRow(
                dismissLabel = context.getString(R.string.cancel),
                confirmLabel = context.getString(R.string.ok),
                onDismiss = { close(onDismiss) },
                onConfirm = { close { onPick(choice) } },
                confirmEnabled = choice.isNotBlank()
            )
        }
    ) {
        Text(context.getString(R.string.macvlan_parent_hint), style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
        PickList(available, Modifier.selectableGroup()) { iface ->
            val on = typed.isBlank() && selected == iface
            OptionRow(
                name = iface,
                modifier = Modifier.selectable(selected = on, role = Role.RadioButton) { selected = iface; typed = "" }
            ) { RadioButton(selected = on, onClick = null) }
        }
        MonoField(
            value = typed,
            onValueChange = { typed = ValidationUtils.ifaceNameInput(it) },
            label = context.getString(R.string.or_type_exact_name),
            placeholder = "eth1"
        )
    }
}

/** Add one port forward, checked against the rules already in the list. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AddPortForwardSheet(existing: List<PortForward>, onDismiss: () -> Unit, onAdd: (PortForward) -> Unit) {
    val context = LocalContext.current
    var hostPort by rememberSaveable { mutableStateOf("") }
    var containerPort by rememberSaveable { mutableStateOf("") }
    var proto by rememberSaveable { mutableStateOf("tcp") }
    val errs = ValidationUtils.validatePortForward(hostPort, containerPort, proto, existing, context)
    val valid = hostPort.isNotBlank() && errs.isValid
    val add = { if (valid) onAdd(PortForward(hostPort.trim(), containerPort.trim().ifEmpty { null }, proto)) }

    DsBottomSheet(
        onDismiss = onDismiss,
        title = context.getString(R.string.add_port_forward),
        footer = {
            DialogFooterRow(
                dismissLabel = context.getString(R.string.cancel),
                confirmLabel = context.getString(R.string.add),
                onDismiss = { close(onDismiss) },
                onConfirm = { if (valid) close(add) },
                confirmEnabled = valid
            )
        }
    ) {
        Text(context.getString(R.string.port_forward_examples), style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)

        @Composable
        fun PortField(value: String, label: String, error: String?, supporting: String?, placeholder: String?, last: Boolean, modifier: Modifier, onChange: (String) -> Unit) {
            OutlinedTextField(
                value = value,
                onValueChange = { if (it.all { c -> c.isDigit() || c == '-' }) onChange(it) },
                label = { Text(label, maxLines = 1) },
                placeholder = placeholder?.let { { Text(it, maxLines = 1) } },
                isError = error != null || (errs.pair != null && value.isNotEmpty()),
                supportingText = (error ?: supporting)?.let { { Text(it) } },
                singleLine = true,
                textStyle = MaterialTheme.typography.bodyLarge.copy(fontFamily = JetBrainsMono),
                shape = RoundedCornerShape(16.dp),
                colors = DsTextFieldDefaults.colors(),
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number, imeAction = if (last) ImeAction.Done else ImeAction.Next),
                keyboardActions = if (last) FocusUtils.clearFocusKeyboardActions() else androidx.compose.foundation.text.KeyboardActions.Default,
                modifier = modifier
            )
        }

        val host = @Composable { m: Modifier ->
            PortField(hostPort, context.getString(R.string.host_port), errs.host, null, null, false, m) { hostPort = it }
        }
        val cont = @Composable { m: Modifier ->
            PortField(
                containerPort, context.getString(R.string.container_port), errs.container,
                context.getString(R.string.optional), context.getString(R.string.same_as_host), true, m
            ) { containerPort = it }
        }
        if (isCompactWidth()) {
            host(Modifier.fillMaxWidth())
            cont(Modifier.fillMaxWidth())
        } else {
            Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                host(Modifier.weight(1f))
                cont(Modifier.weight(1f))
            }
        }
        errs.pair?.let { Text(it, color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodySmall) }

        SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
            listOf("tcp", "udp").forEachIndexed { i, p ->
                SegmentedButton(
                    selected = proto == p,
                    onClick = { proto = p },
                    shape = SegmentedButtonDefaults.itemShape(i, 2)
                ) { Text(p.uppercase()) }
            }
        }
    }
}

/** The interfaces on the phone now, as one rounded list; a line of text when there are none. */
@Composable
private fun PickList(items: List<String>?, modifier: Modifier = Modifier, row: @Composable (String) -> Unit) {
    if (items == null) return
    if (items.isEmpty()) {
        Text(
            LocalContext.current.getString(R.string.no_interfaces_found),
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
        )
        return
    }
    Surface(
        shape = RoundedCornerShape(20.dp),
        color = MaterialTheme.colorScheme.surfaceContainerHigh,
        modifier = modifier.fillMaxWidth()
    ) {
        Column {
            items.forEachIndexed { i, iface ->
                if (i > 0) GroupDivider()
                row(iface)
            }
        }
    }
}

@Composable
private fun OptionRow(name: String, modifier: Modifier, trailing: @Composable () -> Unit) {
    Row(
        modifier = modifier
            .fillMaxWidth()
            .heightIn(min = 56.dp)
            .padding(horizontal = 16.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(
            name,
            style = MaterialTheme.typography.bodyLarge,
            fontFamily = JetBrainsMono,
            fontWeight = FontWeight.Medium,
            modifier = Modifier.weight(1f)
        )
        trailing()
    }
}

/** A checkbox that shows the pick order instead of a tick. */
@Composable
private fun OrderBox(order: Int?) {
    val on = order != null
    Surface(
        shape = RoundedCornerShape(6.dp),
        color = if (on) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.surfaceContainerHigh,
        border = if (on) null else BorderStroke(2.dp, MaterialTheme.colorScheme.onSurfaceVariant),
        modifier = Modifier.size(24.dp)
    ) {
        Box(contentAlignment = Alignment.Center) {
            if (order != null) {
                Text(
                    order.toString(),
                    style = MaterialTheme.typography.labelMedium,
                    fontFamily = JetBrainsMono,
                    fontWeight = FontWeight.Bold,
                    color = MaterialTheme.colorScheme.onPrimary,
                    modifier = Modifier.semantics { contentDescription = order.toString() }
                )
            }
        }
    }
}
