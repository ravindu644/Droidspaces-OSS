package com.droidspaces.app.ui.screen

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.compose.ui.platform.LocalLifecycleOwner
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.repeatOnLifecycle
import com.droidspaces.app.ui.component.EmptyState
import com.droidspaces.app.ui.component.ErrorState
import com.droidspaces.app.ui.component.RootUnavailableState
import com.droidspaces.app.ui.component.PullToRefreshWrapper
import com.droidspaces.app.ui.component.RunningContainerCard
import com.droidspaces.app.ui.viewmodel.ContainerViewModel
import com.droidspaces.app.ui.viewmodel.SystemStatsViewModel
import com.droidspaces.app.util.AnlandUtils
import com.droidspaces.app.util.ContainerManager
import androidx.compose.ui.platform.LocalContext
import com.droidspaces.app.R

/**
 * Control Panel screen - shows system stats and running containers.
 *
 * Note: This screen does NOT have its own PullToRefreshWrapper.
 * The parent ControlPanelTabContent provides the pull-to-refresh functionality.
 * This prevents double-wrapping issues that can cause UI glitches.
 */
@Composable
fun ControlPanelScreen(
    isBackendAvailable: Boolean,
    isRootAvailable: Boolean = true,
    containerViewModel: ContainerViewModel,
    onNavigateToContainerDetails: (String) -> Unit = {},
    onNavigateToTerminal: (String) -> Unit = {},
    emptyStateBottomInset: Dp = 0.dp,
) {
    val context = LocalContext.current
    val snackbarHostState = remember { SnackbarHostState() }
    val systemStatsViewModel: SystemStatsViewModel = viewModel()

    // Get running containers - derived from ViewModel state
    val runningContainers = containerViewModel.containerList.filter { it.isRunning }

    val lifecycleOwner = LocalLifecycleOwner.current

    // Poll only while this screen is composed (i.e. the Panel tab is selected)
    // AND the app is in the foreground (Lifecycle STARTED).  repeatOnLifecycle
    // cancels the loop on background / tab-away and restarts it on return, so
    // re-opening the app on the Panel tab resumes polling without a tab switch.
    // Restarts whenever the running container set changes.
    LaunchedEffect(runningContainers) {
        lifecycleOwner.lifecycle.repeatOnLifecycle(Lifecycle.State.STARTED) {
            systemStatsViewModel.monitorContainers(runningContainers)
        }
    }

    val containerUsageMap = systemStatsViewModel.containerUsageMap

    // Per-container anland display socket (recorded by the native runtime in
    // Pids/<name>.anland). Refreshed whenever the running-container set changes;
    // presence gates the "Launch Anland Window" button.
    val anlandSockets = remember { mutableStateMapOf<String, String>() }
    LaunchedEffect(runningContainers) {
        anlandSockets.clear()
        runningContainers.filter { it.enableAnland }.forEach { c ->
            ContainerManager.getAnlandSocket(c.name)?.let { anlandSockets[c.name] = it }
        }
    }

    Box(modifier = Modifier.fillMaxSize()) {
        // Show content based on root and backend availability
        // Using when instead of early return to prevent UI glitches during recomposition
        when {
            !isRootAvailable -> {
                RootUnavailableState(modifier = Modifier.padding(bottom = emptyStateBottomInset))
            }
            !isBackendAvailable -> {
                ErrorState(modifier = Modifier.padding(bottom = emptyStateBottomInset))
            }
            else -> {
                if (runningContainers.isEmpty()) {
                    EmptyState(
                        icon = Icons.Default.Dashboard,
                        title = context.getString(R.string.no_containers_running),
                        description = context.getString(R.string.start_container_first),
                        // Reserve the floating tab bar's space so the centered
                        // content sits in the visible region, not behind the bar.
                        modifier = Modifier.padding(bottom = emptyStateBottomInset)
                    )
                } else {
                    Column(
                        modifier = Modifier
                            .fillMaxSize()
                            .verticalScroll(rememberScrollState())
                            .padding(horizontal = 16.dp)
                            .padding(top = 8.dp, bottom = 120.dp), // Clear floating tab bar
                        verticalArrangement = Arrangement.spacedBy(16.dp)
                    ) {
                        runningContainers.forEach { container ->
                            val anlandSock = anlandSockets[container.name]
                            RunningContainerCard(
                                container = container,
                                onEnter = {
                                    onNavigateToContainerDetails(container.name)
                                },
                                onTerminalClick = {
                                    onNavigateToTerminal(container.name)
                                },
                                anlandEnabled = container.enableAnland && anlandSock != null,
                                onLaunchAnland = {
                                    anlandSock?.let { AnlandUtils.launchWindow(context, container.name, it) }
                                },
                                osInfo = containerUsageMap[container.name],
                            )
                        }
                    }
                }
            }
        }

        // Snackbar host (always present)
        SnackbarHost(
            hostState = snackbarHostState,
            modifier = Modifier.align(Alignment.BottomCenter)
        )
    }
}

