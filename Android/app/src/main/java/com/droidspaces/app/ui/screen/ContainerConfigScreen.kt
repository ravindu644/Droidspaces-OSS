package com.droidspaces.app.ui.screen

import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.consumeWindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.ArrowForward
import androidx.compose.material.ripple.rememberRipple
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.droidspaces.app.R
import com.droidspaces.app.ui.component.ContainerConfigForm
import com.droidspaces.app.ui.util.ClearFocusOnClickOutside
import com.droidspaces.app.util.ContainerConfigState
import com.droidspaces.app.util.ContainerInfo
import com.droidspaces.app.util.ValidationUtils
import com.droidspaces.app.util.isDirectNetworkValid

@OptIn(ExperimentalMaterial3Api::class)
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
        state.isDirectNetworkValid() && collisionContainer == null

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(context.getString(R.string.configuration_title)) },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = context.getString(R.string.back))
                    }
                }
            )
        },
        bottomBar = {
            val btnShape = RoundedCornerShape(20.dp)
            Surface(
                modifier = Modifier.fillMaxWidth(),
                color = MaterialTheme.colorScheme.surfaceContainer,
                tonalElevation = 0.dp
            ) {
                Column(modifier = Modifier.fillMaxWidth()) {
                    HorizontalDivider(
                        color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.25f),
                        thickness = 1.dp
                    )
                    Surface(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(24.dp)
                            .navigationBarsPadding()
                            .clip(btnShape)
                            .clickable(
                                enabled = canProceed,
                                onClick = { onNext(state) },
                                indication = rememberRipple(bounded = true),
                                interactionSource = remember { MutableInteractionSource() }
                            ),
                        shape = btnShape,
                        color = if (canProceed) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.12f),
                        tonalElevation = 0.dp
                    ) {
                        Box(modifier = Modifier.padding(vertical = 16.dp), contentAlignment = Alignment.Center) {
                            Row(
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(8.dp)
                            ) {
                                Icon(
                                    Icons.AutoMirrored.Filled.ArrowForward,
                                    contentDescription = null,
                                    modifier = Modifier.size(18.dp),
                                    tint = if (canProceed) MaterialTheme.colorScheme.onPrimary else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f)
                                )
                                Text(
                                    context.getString(R.string.next_storage),
                                    style = MaterialTheme.typography.labelLarge,
                                    fontWeight = FontWeight.SemiBold,
                                    color = if (canProceed) MaterialTheme.colorScheme.onPrimary else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f)
                                )
                            }
                        }
                    }
                }
            }
        }
    ) { innerPadding ->
        ClearFocusOnClickOutside(
            modifier = Modifier
                .padding(innerPadding)
                .consumeWindowInsets(innerPadding)
                .imePadding()
        ) {
            ContainerConfigForm(
                state = state,
                onStateChange = { state = it },
                installedContainers = installedContainers,
                selfName = containerName,
                gatewayErrors = gatewayErrors,
                collisionContainer = collisionContainer,
                modifier = Modifier.fillMaxSize(),
                leadingContent = {
                    Text(
                        text = context.getString(R.string.container_options),
                        style = MaterialTheme.typography.headlineSmall,
                        fontWeight = FontWeight.Bold
                    )
                }
            )
        }
    }
}
