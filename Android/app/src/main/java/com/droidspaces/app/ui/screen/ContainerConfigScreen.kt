package com.droidspaces.app.ui.screen

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowForward
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.platform.LocalContext
import com.droidspaces.app.R
import com.droidspaces.app.ui.component.ContainerConfigHost
import com.droidspaces.app.ui.component.PrimaryActionBottomBar
import com.droidspaces.app.ui.component.screenGutter
import com.droidspaces.app.util.ResourceLimits
import com.droidspaces.app.util.ContainerConfigState
import com.droidspaces.app.util.ContainerInfo
import com.droidspaces.app.util.ValidationUtils

@Composable
fun ContainerConfigScreen(
    initialState: ContainerConfigState = ContainerConfigState(),
    containerName: String = "",
    installedContainers: List<ContainerInfo> = emptyList(),
    onNext: (ContainerConfigState) -> Unit,
    onBack: () -> Unit
) {
    val context = LocalContext.current
    var state by remember { mutableStateOf(initialState) }

    val macvlanErrors = ValidationUtils.validateMacvlanConfig(
        selfName = containerName,
        netMode = state.netMode,
        parent = state.macvlanParent,
        mode = state.macvlanMode,
        installed = installedContainers,
        context = context
    )

    val gatewayErrors = ValidationUtils.validateGatewayConfig(
        selfName = containerName,
        gatewayContainer = state.gatewayContainer,
        net = state.gatewayNet,
        iface = state.gatewayIface,
        bridge = state.gatewayBridge,
        installed = installedContainers,
        context = context
    )

    val collisionContainer = remember(state.netMode, state.staticNatIp, installedContainers) {
        if (state.netMode != "nat" || state.staticNatIp.isEmpty()) null
        else installedContainers.find { it.name != containerName && it.staticNatIp == state.staticNatIp }
    }

    val canProceed = (state.netMode != "gateway" || gatewayErrors.isValid) &&
        macvlanErrors.isValid && collisionContainer == null &&
        ResourceLimits.isValidPidsLimit(state.pidsLimit)

    ContainerConfigHost(
        title = context.getString(R.string.configuration_title),
        onBack = onBack,
        bottomBar = {
            PrimaryActionBottomBar(
                label = context.getString(R.string.next_storage),
                icon = Icons.AutoMirrored.Filled.ArrowForward,
                onClick = { onNext(state) },
                enabled = canProceed,
                horizontalPadding = screenGutter()
            )
        },
        state = state,
        onStateChange = { state = it },
        installedContainers = installedContainers,
        selfName = containerName,
        gatewayErrors = gatewayErrors,
        macvlanErrors = macvlanErrors,
        collisionContainer = collisionContainer
    )
}
