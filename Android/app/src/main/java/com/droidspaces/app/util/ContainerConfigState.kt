package com.droidspaces.app.util

/**
 * Immutable holder for the ~28 editable container-configuration fields that the
 * Create wizard ([com.droidspaces.app.ui.screen.ContainerConfigScreen]) and the
 * Edit screen ([com.droidspaces.app.ui.screen.EditContainerScreen]) both drive
 * through the shared [com.droidspaces.app.ui.component.ContainerConfigForm].
 *
 * Replaces the four hand-maintained copies of this field list that previously
 * lived as: editable `var`s, parallel `saved*` `var`s, a 28-arg `onNext`
 * callback, and a 28-arg `setConfig`. A single data class means `hasChanges`
 * is just `current != saved`, and config plumbing can never transpose fields.
 *
 * Note: `name`, `hostname`, `rootfsPath`, sparse-image and runtime fields are
 * NOT part of this state — they are owned by the surrounding screens/ViewModel.
 */
data class ContainerConfigState(
    val netMode: String = "nat",
    val netParent: String = "",
    val netMac: String = "",
    val netIpam: String = "dhcp",
    val hostAccess: String = "none",
    val netAddress: String = "",
    val netGateway: String = "",
    val disableIPv6: Boolean = false,
    val enableAndroidStorage: Boolean = false,
    val enableHwAccess: Boolean = false,
    val enableGpuMode: Boolean = false,
    val enableTermuxX11: Boolean = false,
    val tx11ExtraFlags: String = "",
    val enableVirgl: Boolean = false,
    val virglExtraFlags: String = "",
    val enablePulseaudio: Boolean = false,
    val selinuxPermissive: Boolean = false,
    val allowUserns: Boolean = false,
    val volatileMode: Boolean = false,
    val bindMounts: List<BindMount> = emptyList(),
    val dnsServers: String = "",
    val runAtBoot: Boolean = false,
    val customInit: String = "",
    val staticNatIp: String = "",
    val forceCgroupv1: Boolean = false,
    val blockNestedNs: Boolean = false,
    val privileged: String = "",
    val envFileContent: String = "",
    val upstreamInterfaces: List<String> = emptyList(),
    val portForwards: List<PortForward> = emptyList(),
    val gatewayContainer: String = "",
    val gatewayNet: String = "",
    val gatewayIface: String = "",
    val gatewayBridge: String = "",
)

/** Extract the editable config fields from an existing container. */
fun ContainerInfo.toConfigState(): ContainerConfigState = ContainerConfigState(
    netMode = netMode,
    netParent = netParent,
    netMac = netMac,
    netIpam = netIpam,
    hostAccess = hostAccess,
    netAddress = netAddress,
    netGateway = netGateway,
    disableIPv6 = disableIPv6,
    enableAndroidStorage = enableAndroidStorage,
    enableHwAccess = enableHwAccess,
    enableGpuMode = enableGpuMode,
    enableTermuxX11 = enableTermuxX11,
    tx11ExtraFlags = tx11ExtraFlags,
    enableVirgl = enableVirgl,
    virglExtraFlags = virglExtraFlags,
    enablePulseaudio = enablePulseaudio,
    selinuxPermissive = selinuxPermissive,
    allowUserns = allowUserns,
    volatileMode = volatileMode,
    bindMounts = bindMounts,
    dnsServers = dnsServers,
    runAtBoot = runAtBoot,
    customInit = customInit,
    staticNatIp = staticNatIp,
    forceCgroupv1 = forceCgroupv1,
    blockNestedNs = blockNestedNs,
    privileged = privileged,
    envFileContent = envFileContent ?: "",
    upstreamInterfaces = upstreamInterfaces,
    portForwards = portForwards,
    gatewayContainer = gatewayContainer,
    gatewayNet = gatewayNet,
    gatewayIface = gatewayIface,
    gatewayBridge = gatewayBridge,
)

/**
 * Return a copy of this container with the editable config fields replaced by
 * [state]. Non-config fields (name, rootfsPath, status, sparse image,
 * runAtBootPriority, uuid, …) are preserved.
 */
fun ContainerInfo.withConfig(state: ContainerConfigState): ContainerInfo = copy(
    netMode = state.netMode,
    netParent = state.netParent,
    netMac = state.netMac,
    netIpam = state.netIpam,
    hostAccess = state.hostAccess,
    netAddress = state.netAddress,
    netGateway = state.netGateway,
    disableIPv6 = state.disableIPv6,
    enableAndroidStorage = state.enableAndroidStorage,
    enableHwAccess = state.enableHwAccess,
    enableGpuMode = state.enableGpuMode,
    enableTermuxX11 = state.enableTermuxX11,
    tx11ExtraFlags = state.tx11ExtraFlags,
    enableVirgl = state.enableVirgl,
    virglExtraFlags = state.virglExtraFlags,
    enablePulseaudio = state.enablePulseaudio,
    selinuxPermissive = state.selinuxPermissive,
    allowUserns = state.allowUserns,
    volatileMode = state.volatileMode,
    bindMounts = state.bindMounts,
    dnsServers = state.dnsServers,
    runAtBoot = state.runAtBoot,
    customInit = state.customInit,
    staticNatIp = state.staticNatIp,
    forceCgroupv1 = state.forceCgroupv1,
    blockNestedNs = state.blockNestedNs,
    privileged = state.privileged,
    envFileContent = state.envFileContent.ifBlank { null },
    upstreamInterfaces = state.upstreamInterfaces,
    portForwards = state.portForwards,
    gatewayContainer = state.gatewayContainer,
    gatewayNet = state.gatewayNet,
    gatewayIface = state.gatewayIface,
    gatewayBridge = state.gatewayBridge,
)

private fun String.isValidIpv4(): Boolean {
    val octets = split('.')
    return octets.size == 4 && octets.all {
        it.isNotEmpty() && it.length <= 3 && it.all(Char::isDigit) &&
            (it.toIntOrNull() ?: -1) in 0..255
    }
}

private fun String.isValidUnicastMac(): Boolean {
    val octets = split(':')
    if (octets.size != 6 || octets.any { it.length != 2 || it.toIntOrNull(16) == null }) return false
    val bytes = octets.map { it.toInt(16) }
    return bytes.any { it != 0 } && (bytes[0] and 1) == 0
}

fun ContainerConfigState.isNetMacValid(): Boolean =
    netMode != "macvlan" || netMac.isBlank() || netMac.isValidUnicastMac()

/** Validation shared by create/edit action gating and the direct-L2 form. */
fun ContainerConfigState.isDirectNetworkValid(): Boolean {
    if (netMode != "ipvlan" && netMode != "macvlan") return true
    // Blank selects the backend's Android-aware active-uplink detection.
    if (netParent.isNotBlank() && (netParent.length >= 16 ||
        netParent.any { it.isWhitespace() || it == '/' })) return false
    if (!isNetMacValid()) return false
    if (netIpam == "dhcp") return true
    if (netIpam != "static") return false
    val parts = netAddress.split('/', limit = 2)
    val prefix = parts.getOrNull(1)?.toIntOrNull()
    return parts.size == 2 && parts[0].isValidIpv4() && prefix != null &&
        prefix in 0..32 &&
        netGateway.isValidIpv4()
}
