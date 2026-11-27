package com.droidspaces.app.ui.component

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.DragHandle
import androidx.compose.material.icons.filled.Lan
import androidx.compose.material.icons.filled.Router
import androidx.compose.material.icons.filled.SwapHoriz
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.animation.shrinkVertically
import androidx.compose.animation.expandVertically
import androidx.compose.animation.AnimatedVisibility
import com.droidspaces.app.util.AnimationUtils
import androidx.compose.animation.togetherWith
import androidx.compose.animation.fadeOut
import androidx.compose.animation.fadeIn
import androidx.compose.animation.AnimatedContent
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.foundation.relocation.bringIntoViewRequester
import androidx.compose.foundation.relocation.BringIntoViewRequester
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import com.droidspaces.app.R
import com.droidspaces.app.ui.util.FocusUtils
import com.droidspaces.app.util.Constants
import com.droidspaces.app.util.ContainerConfigState
import com.droidspaces.app.util.ContainerInfo
import com.droidspaces.app.util.GatewayErrors
import com.droidspaces.app.util.HostCapabilities
import com.droidspaces.app.util.MacvlanErrors
import com.droidspaces.app.util.ValidationUtils
import kotlinx.coroutines.delay
import sh.calvin.reorderable.ReorderableColumn

/** Network modes in the order the page lists them, most used first. */
private val MODE_ORDER = listOf("nat", "gateway", "macvlan", "host", "none")

/** The macvlan modes the backend accepts, default first. */
private val MACVLAN_MODES = listOf("bridge", "private", "vepa", "passthru")

private const val MAX_UPSTREAMS = 8
private const val MAX_PORT_FORWARDS = 32

fun netModeLabel(mode: String) = when (mode) {
    "nat" -> R.string.network_mode_nat
    "none" -> R.string.network_mode_none
    "gateway" -> R.string.network_mode_gateway
    "macvlan" -> R.string.network_mode_macvlan
    else -> R.string.network_mode_host
}

private fun netModeDescription(mode: String) = when (mode) {
    "nat" -> R.string.network_mode_nat_desc
    "none" -> R.string.network_mode_none_desc
    "gateway" -> R.string.network_mode_gateway_desc
    "macvlan" -> R.string.network_mode_macvlan_desc
    else -> R.string.network_mode_host_desc
}

private fun macvlanModeName(mode: String) = when (mode) {
    "private" -> R.string.macvlan_mode_private
    "vepa" -> R.string.macvlan_mode_vepa
    "passthru" -> R.string.macvlan_mode_passthru
    else -> R.string.macvlan_mode_bridge
}

private fun macvlanModeDescription(mode: String) = when (mode) {
    "private" -> R.string.macvlan_mode_private_desc
    "vepa" -> R.string.macvlan_mode_vepa_desc
    "passthru" -> R.string.macvlan_mode_passthru_desc
    else -> R.string.macvlan_mode_bridge_desc
}

/**
 * The network mode list and, under it, everything the chosen mode needs, as a
 * draft that leaving keeps, unless the chosen mode's settings would not save:
 * a gateway or macvlan error, a static IP another container holds, or an
 * octet out of range or missing its pair. Then leaving asks first.
 */
@Composable
fun NetworkModePage(
    state: ContainerConfigState,
    onSave: (ContainerConfigState) -> Unit,
    onClose: () -> Unit,
    installedContainers: List<ContainerInfo>,
    selfName: String,
) {
    val context = LocalContext.current
    val caps by HostCapabilities.state.collectAsState()
    val supported = caps?.supportedNetModes() ?: HostCapabilities.ALL_NET_MODES
    var draft by remember { mutableStateOf(state) }
    val set: (ContainerConfigState) -> Unit = { draft = it }

    val gatewayErrors = ValidationUtils.validateGatewayConfig(
        selfName = selfName,
        gatewayContainer = draft.gatewayContainer,
        net = draft.gatewayNet,
        iface = draft.gatewayIface,
        bridge = draft.gatewayBridge,
        installed = installedContainers,
        context = context
    )
    val macvlanErrors = ValidationUtils.validateMacvlanConfig(
        selfName = selfName,
        netMode = draft.netMode,
        parent = draft.macvlanParent,
        mode = draft.macvlanMode,
        installed = installedContainers,
        context = context
    )
    val collision = if (draft.netMode != "nat" || draft.staticNatIp.isEmpty()) null
        else installedContainers.find { it.name != selfName && it.staticNatIp == draft.staticNatIp }
    val canSave = when (draft.netMode) {
        "nat" -> collision == null && natIpValid(draft.staticNatIp)
        "gateway" -> gatewayErrors.isValid
        "macvlan" -> macvlanErrors.isValid
        else -> true
    }

    DraftPageScaffold(
        title = context.getString(R.string.network_mode),
        dirty = state.withNetworkOf(draft) != state,
        canSave = canSave,
        onSave = { onSave(draft) },
        onClose = onClose
    ) {
        Spacer(Modifier.height(16.dp))
        SettingsGroup(Modifier.selectableGroup()) {
            MODE_ORDER.filter { it in supported }.forEachIndexed { i, mode ->
                if (i > 0) GroupDivider()
                RadioRow(
                    title = context.getString(netModeLabel(mode)),
                    description = context.getString(netModeDescription(mode)),
                    selected = draft.netMode == mode,
                    onClick = { set(draft.copy(netMode = mode)) }
                )
            }
        }

        when (draft.netMode) {
            "nat" -> NatSettings(draft, set, collision)
            "gateway" -> GatewaySettings(draft, set, installedContainers, selfName, gatewayErrors)
            "macvlan" -> MacvlanSettings(draft, set, macvlanErrors)
        }
    }
}

/**
 * This state with the network page's fields taken from [page]. The page edits
 * a copy of the whole state but owns only these, so saving or comparing it
 * never touches what the rest of the screen changed meanwhile.
 */
fun ContainerConfigState.withNetworkOf(page: ContainerConfigState) = copy(
    netMode = page.netMode,
    staticNatIp = page.staticNatIp,
    upstreamInterfaces = page.upstreamInterfaces,
    portForwards = page.portForwards,
    gatewayContainer = page.gatewayContainer,
    gatewayNet = page.gatewayNet,
    gatewayIface = page.gatewayIface,
    gatewayBridge = page.gatewayBridge,
    macvlanParent = page.macvlanParent,
    macvlanMode = page.macvlanMode,
)

/** An empty static IP auto-assigns; a set one needs both octets in range. */
private fun natIpValid(ip: String): Boolean {
    if (ip.isEmpty()) return true
    val parts = ip.split(".")
    return parts.size == 4 && parts.drop(2).all { o ->
        o.toIntOrNull()?.let { it in Constants.NAT_OCTET_MIN..Constants.NAT_OCTET_MAX } == true
    }
}

@Composable
private fun NatSettings(
    state: ContainerConfigState,
    onStateChange: (ContainerConfigState) -> Unit,
    collisionContainer: ContainerInfo?,
) {
    val context = LocalContext.current
    var showUpstreamSheet by rememberSaveable { mutableStateOf(false) }
    var showPortSheet by rememberSaveable { mutableStateOf(false) }

    GroupHeader(context.getString(R.string.static_ip_address), sub = true)
    GroupIntro(context.getString(R.string.static_ip_description))
    SettingsGroup {
        GroupField {
            StaticIpFields(state, onStateChange)
            if (collisionContainer != null) {
                Text(
                    context.getString(R.string.error_ip_collision, collisionContainer.name),
                    color = MaterialTheme.colorScheme.error,
                    style = MaterialTheme.typography.bodySmall,
                    modifier = Modifier.padding(top = 4.dp)
                )
            }
        }
    }

    GroupHeader(context.getString(R.string.upstream_interface_title), sub = true)
    GroupIntro(context.getString(R.string.upstream_interface_hint))
    EntryGroup(visible = state.upstreamInterfaces.isNotEmpty()) {
        ReorderableColumn(
            list = state.upstreamInterfaces,
            onSettle = { from, to ->
                onStateChange(state.copy(upstreamInterfaces = state.upstreamInterfaces.toMutableList().apply { add(to, removeAt(from)) }))
            }
        ) { index, iface, _ ->
            key(iface) {
                ReorderableItem {
                    AnimatedRemoval(onRemoved = { onStateChange(state.copy(upstreamInterfaces = state.upstreamInterfaces - iface)) }) { remove ->
                        Column {
                            if (index > 0) GroupDivider()
                            EntryRow(
                                text = iface,
                                deleteDescription = context.getString(R.string.delete_item, iface),
                                onDelete = remove,
                                leading = {
                                    Icon(
                                        Icons.Default.DragHandle,
                                        contentDescription = context.getString(R.string.reorder),
                                        tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f),
                                        modifier = Modifier.draggableHandle()
                                    )
                                }
                            )
                        }
                    }
                }
            }
        }
    }
    if (state.upstreamInterfaces.size < MAX_UPSTREAMS) {
        GroupAddButton(context.getString(R.string.add_upstream_interface), count = state.upstreamInterfaces.size) { showUpstreamSheet = true }
    }

    GroupHeader(context.getString(R.string.port_forwarding), sub = true)
    GroupIntro(context.getString(R.string.port_forwarding_intro))
    EntryGroup(visible = state.portForwards.isNotEmpty()) {
        state.portForwards.forEachIndexed { i, pf ->
            key(pf) {
                AnimatedRemoval(onRemoved = { onStateChange(state.copy(portForwards = state.portForwards - pf)) }) { remove ->
                    Column {
                        if (i > 0) GroupDivider()
                        val proto = pf.proto.uppercase()
                        EntryRow(
                            text = if (pf.containerPort != null) "${pf.hostPort} → ${pf.containerPort}" else pf.hostPort,
                            supporting = if (pf.containerPort != null) proto else context.getString(R.string.port_forward_same_port, proto),
                            deleteDescription = context.getString(R.string.delete_item, pf.hostPort),
                            onDelete = remove,
                            leading = { Icon(Icons.Default.SwapHoriz, contentDescription = null, tint = MaterialTheme.colorScheme.onSurfaceVariant) }
                        )
                    }
                }
            }
        }
    }
    if (state.portForwards.size < MAX_PORT_FORWARDS) {
        GroupAddButton(context.getString(R.string.add_port_forward), count = state.portForwards.size) { showPortSheet = true }
    }

    if (showUpstreamSheet) {
        UpstreamPickerSheet(
            current = state.upstreamInterfaces,
            max = MAX_UPSTREAMS,
            onDismiss = { showUpstreamSheet = false },
            onDone = {
                onStateChange(state.copy(upstreamInterfaces = it))
                showUpstreamSheet = false
            }
        )
    }
    if (showPortSheet) {
        AddPortForwardSheet(
            existing = state.portForwards,
            onDismiss = { showPortSheet = false },
            onAdd = {
                onStateChange(state.copy(portForwards = state.portForwards + it))
                showPortSheet = false
            }
        )
    }
}

/**
 * The last two octets of the NAT address after a fixed 172.28. prefix, the row
 * the form has always had. The prefix texts sit 8dp low to line up with the
 * fields' text rather than their floating labels.
 */
@Composable
private fun StaticIpFields(state: ContainerConfigState, onStateChange: (ContainerConfigState) -> Unit) {
    val context = LocalContext.current
    val octets = remember(state.staticNatIp) {
        val parts = state.staticNatIp.split(".")
        if (parts.size == 4) parts[2] to parts[3] else "" to ""
    }
    var octet3 by remember(octets) { mutableStateOf(octets.first) }
    var octet4 by remember(octets) { mutableStateOf(octets.second) }
    val update = { o3: String, o4: String ->
        onStateChange(state.copy(staticNatIp = if (o3.isBlank() && o4.isBlank()) "" else "${Constants.NAT_IP_PREFIX}.$o3.$o4"))
    }
    fun valid(o: String) = o.isEmpty() || o.toIntOrNull()?.let { it in Constants.NAT_OCTET_MIN..Constants.NAT_OCTET_MAX } == true

    @Composable
    fun RowScope.OctetField(value: String, n: Int, last: Boolean, onChange: (String) -> Unit) {
        OutlinedTextField(
            value = value,
            onValueChange = { if (it.length <= 3 && it.all(Char::isDigit)) onChange(it) },
            label = { Text(context.getString(R.string.octet_label, n), maxLines = 1) },
            modifier = Modifier.weight(1f),
            singleLine = true,
            shape = RoundedCornerShape(16.dp),
            colors = DsTextFieldDefaults.colors(),
            isError = !valid(value),
            supportingText = { if (!valid(value)) Text(context.getString(R.string.error_octet_range)) },
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number, imeAction = if (last) ImeAction.Done else ImeAction.Next),
            keyboardActions = if (last) FocusUtils.clearFocusKeyboardActions() else androidx.compose.foundation.text.KeyboardActions.Default
        )
    }

    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text("${Constants.NAT_IP_PREFIX}.", style = MaterialTheme.typography.bodyLarge, modifier = Modifier.padding(top = 8.dp))
        OctetField(octet3, 3, last = false) { octet3 = it; update(it, octet4) }
        Text(".", style = MaterialTheme.typography.bodyLarge, modifier = Modifier.padding(top = 8.dp))
        OctetField(octet4, 4, last = true) { octet4 = it; update(octet3, it) }
    }
}

@Composable
private fun GatewaySettings(
    state: ContainerConfigState,
    onStateChange: (ContainerConfigState) -> Unit,
    installedContainers: List<ContainerInfo>,
    selfName: String,
    errors: GatewayErrors,
) {
    val context = LocalContext.current
    val candidates = remember(installedContainers, selfName) { installedContainers.map { it.name }.filter { it != selfName } }

    GroupHeader(context.getString(R.string.gateway_container), sub = true)
    GroupIntro(context.getString(R.string.gateway_settings_description))
    SettingsGroup {
        GroupField {
            DsDropdown(
                label = context.getString(R.string.gateway_container),
                selected = state.gatewayContainer,
                options = candidates,
                displayName = { it },
                onSelect = { onStateChange(state.copy(gatewayContainer = it)) },
                leadingIcon = Icons.Default.Router,
                isError = errors.container != null,
                supportingText = if (candidates.isEmpty()) context.getString(R.string.error_no_gateway_candidates) else errors.container,
                enabled = candidates.isNotEmpty()
            )
        }
    }

    GroupHeader(context.getString(R.string.gateway_lan), sub = true)
    GroupIntro(context.getString(R.string.gateway_configure_intro))
    SettingsGroup {
        GroupField {
            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                LanField(
                    label = context.getString(R.string.gateway_iface),
                    value = state.gatewayIface,
                    hint = context.getString(R.string.gateway_iface_hint),
                    explain = context.getString(R.string.gateway_iface_explain),
                    error = errors.iface,
                    last = false
                ) { onStateChange(state.copy(gatewayIface = it)) }
                LanField(
                    label = context.getString(R.string.gateway_net),
                    value = state.gatewayNet,
                    hint = context.getString(R.string.gateway_net_hint),
                    explain = context.getString(R.string.gateway_net_explain),
                    error = errors.net,
                    last = false
                ) { onStateChange(state.copy(gatewayNet = it)) }
                LanField(
                    label = context.getString(R.string.gateway_bridge),
                    value = state.gatewayBridge,
                    hint = context.getString(R.string.gateway_bridge_hint),
                    explain = context.getString(R.string.gateway_bridge_explain),
                    error = errors.bridge,
                    last = true
                ) { onStateChange(state.copy(gatewayBridge = it)) }
            }
        }
    }
}

@Composable
private fun LanField(
    label: String,
    value: String,
    hint: String,
    explain: String,
    error: String?,
    last: Boolean,
    onChange: (String) -> Unit,
) {
    MonoField(
        value = value,
        onValueChange = { onChange(ValidationUtils.ifaceNameInput(it)) },
        label = label,
        placeholder = hint,
        supporting = error ?: explain,
        isError = error != null,
        imeAction = if (last) ImeAction.Done else ImeAction.Next
    )
}

@Composable
private fun MacvlanSettings(
    state: ContainerConfigState,
    onStateChange: (ContainerConfigState) -> Unit,
    errors: MacvlanErrors,
) {
    val context = LocalContext.current
    var showPicker by rememberSaveable { mutableStateOf(false) }

    GroupHeader(context.getString(R.string.macvlan_parent), sub = true)
    GroupIntro(context.getString(R.string.macvlan_parent_hint))
    // Swapping the interface card for the choose button crossfades and eases the
    // height between them, instead of the section snapping to its new size.
    AnimatedContent(
        targetState = state.macvlanParent,
        transitionSpec = { fadeIn(AnimationUtils.fastSpec()) togetherWith fadeOut(AnimationUtils.fastSpec()) },
        label = "macvlan_parent"
    ) { parent ->
        Column {
            if (parent.isBlank()) {
                GroupAddButton(context.getString(R.string.macvlan_parent_choose)) { showPicker = true }
            } else {
                SettingsGroup {
                    EntryRow(
                        text = parent,
                        deleteDescription = context.getString(R.string.delete_item, parent),
                        onDelete = { onStateChange(state.copy(macvlanParent = "")) },
                        leading = { Icon(Icons.Default.Lan, contentDescription = null, tint = MaterialTheme.colorScheme.onSurfaceVariant) }
                    )
                }
            }
        }
    }
    (errors.parent ?: errors.warning)?.let {
        Text(
            it,
            style = MaterialTheme.typography.bodySmall,
            color = if (errors.parent != null) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f),
            modifier = Modifier.padding(start = screenGutter() + 16.dp, end = screenGutter() + 16.dp, top = 8.dp)
        )
    }

    GroupHeader(context.getString(R.string.macvlan_mode), sub = true)
    val selected = state.macvlanMode.ifBlank { "bridge" }
    SettingsGroup(Modifier.selectableGroup()) {
        MACVLAN_MODES.forEachIndexed { i, mode ->
            if (i > 0) GroupDivider()
            RadioRow(
                title = context.getString(macvlanModeName(mode)),
                description = context.getString(macvlanModeDescription(mode)),
                selected = selected == mode,
                onClick = { onStateChange(state.copy(macvlanMode = mode)) }
            )
        }
    }

    if (showPicker) {
        HostInterfaceSheet(
            onDismiss = { showPicker = false },
            onPick = {
                onStateChange(state.copy(macvlanParent = it))
                showPicker = false
            }
        )
    }
}

/** One option of a radio list inside a [SettingsGroup]; the whole row is the target. */
@Composable
fun RadioRow(title: String, description: String?, selected: Boolean, onClick: () -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .selectable(selected = selected, role = Role.RadioButton, onClick = onClick)
            .heightIn(min = if (description != null) 72.dp else 56.dp)
            .padding(horizontal = 16.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        // A 24dp box for the 20dp radio, so the text starts where an icon row's does.
        Box(Modifier.size(24.dp), contentAlignment = Alignment.Center) {
            RadioButton(selected = selected, onClick = null)
        }
        Spacer(Modifier.width(16.dp))
        Column(Modifier.weight(1f)) {
            Text(
                title,
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.SemiBold,
                color = if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurface
            )
            if (description != null) {
                Text(description, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }
    }
}

/**
 * The add button under a list, the card button the form has always used,
 * outside the group so it reads as an action rather than another entry.
 * When the list above grows
 * by [count], the page scrolls the button back into view, so adding a fourth
 * interface does not leave the user hunting for the button.
 */
@Composable
fun GroupAddButton(label: String, count: Int = 0, onClick: () -> Unit) {
    val requester = remember { BringIntoViewRequester() }
    var last by remember { mutableIntStateOf(count) }
    LaunchedEffect(count) {
        if (count > last) {
            // The new entry expands in on fastSpec, from zero height. A request made
            // now is already satisfied, the button is still on screen, and the entry
            // then grows under it and pushes it off. Ask once the entry has its height.
            delay(AnimationUtils.DURATION_FAST.toLong())
            requester.bringIntoView()
        }
        last = count
    }
    AddEntryButton(
        label = label,
        onClick = onClick,
        modifier = Modifier
            .bringIntoViewRequester(requester)
            .padding(horizontal = screenGutter())
    )
}

/**
 * The group a list's entries sit in, with the gap to the add button under it.
 * Both fold away with the last entry and open with the first, instead of the
 * border and the gap popping in and out of the page.
 */
@Composable
fun EntryGroup(visible: Boolean, content: @Composable ColumnScope.() -> Unit) {
    AnimatedVisibility(
        visible = visible,
        enter = expandVertically(AnimationUtils.fastSpec()) + fadeIn(AnimationUtils.fastSpec()),
        exit = shrinkVertically(AnimationUtils.fastSpec()) + fadeOut(AnimationUtils.fastSpec())
    ) {
        Column {
            // Remembered inside the group, so entries composed while the group
            // itself expands in ride its animation instead of adding their own.
            var settled by remember { mutableStateOf(false) }
            LaunchedEffect(Unit) { settled = true }
            CompositionLocalProvider(LocalEntryGroupSettled provides settled) {
                SettingsGroup(content = content)
            }
            Spacer(Modifier.height(12.dp))
        }
    }
}
