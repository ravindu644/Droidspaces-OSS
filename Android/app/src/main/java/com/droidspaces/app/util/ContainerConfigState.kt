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
 * NOT part of this state. They are owned by the surrounding screens/ViewModel.
 */
data class ContainerConfigState(
    val netMode: String = "nat",
    val disableIPv6: Boolean = false,
    val enableAndroidStorage: Boolean = false,
    val enableHwAccess: Boolean = false,
    val enableGpuMode: Boolean = false,
    val enableTermuxX11: Boolean = false,
    val tx11ExtraFlags: String = "",
    val enableAnland: Boolean = false,
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
    disableIPv6 = disableIPv6,
    enableAndroidStorage = enableAndroidStorage,
    enableHwAccess = enableHwAccess,
    enableGpuMode = enableGpuMode,
    enableTermuxX11 = enableTermuxX11,
    tx11ExtraFlags = tx11ExtraFlags,
    enableAnland = enableAnland,
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
    disableIPv6 = state.disableIPv6,
    enableAndroidStorage = state.enableAndroidStorage,
    enableHwAccess = state.enableHwAccess,
    enableGpuMode = state.enableGpuMode,
    enableTermuxX11 = state.enableTermuxX11,
    tx11ExtraFlags = state.tx11ExtraFlags,
    enableAnland = state.enableAnland,
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
