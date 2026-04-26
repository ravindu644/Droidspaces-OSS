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
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.droidspaces.app.ui.component.EmptyState
import com.droidspaces.app.ui.component.ErrorState
import com.droidspaces.app.ui.component.RootUnavailableState
import com.droidspaces.app.ui.component.PullToRefreshWrapper
import com.droidspaces.app.ui.component.RunningContainerCard
import com.droidspaces.app.ui.component.SystemStatisticsCard
import com.droidspaces.app.ui.viewmodel.ContainerViewModel
import com.droidspaces.app.ui.viewmodel.SystemStatsViewModel
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
    onNavigateToToTerminal: (String) -> Unit = {},
) {
    val context = LocalContext.current
    val snackbarHostState = remember { SnackbarHostState() }
    val systemStatsViewModel: SystemStatsViewModel = viewModel()

    // Get running containers - derived from ViewModel state
    val runningContainers = containerViewModel.containerList.filter { it.isRunning }

    // Start system stats monitoring (only once per screen lifetime)
    LaunchedEffect(Unit) {
        systemStatsViewModel.startMonitoring()
    }

    // Get system usage stats
    val systemUsage = systemStatsViewModel.systemUsage

    Box(modifier = Modifier.fillMaxSize()) {
        // Show content based on root and backend availability
        // Using when instead of early return to prevent UI glitches during recomposition
        when {
            !isRootAvailable -> {
                RootUnavailableState()
            }
            !isBackendAvailable -> {
                ErrorState()
            }
            else -> {
                // Main content - System stats and running containers
                Column(
                    modifier = Modifier
                        .fillMaxSize()
                        .verticalScroll(rememberScrollState())
                        .padding(16.dp),
                    verticalArrangement = Arrangement.spacedBy(16.dp)
                ) {
                    // Section 1: System Statistics (always visible)
                    SystemStatisticsCard(
                        cpuPercent = systemUsage.cpuPercent,
                        ramPercent = systemUsage.ramPercent,
                        temperature = systemUsage.temperature
                    )

                    // Horizontal divider
                    HorizontalDivider(
                        modifier = Modifier.padding(vertical = 8.dp),
                        thickness = 1.dp,
                        color = MaterialTheme.colorScheme.outlineVariant
                    )

                    // Section 2: Active Containers
                    Text(
                        text = context.getString(R.string.active_containers),
                        style = MaterialTheme.typography.titleSmall,
                        fontWeight = FontWeight.SemiBold,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.padding(bottom = 8.dp)
                    )

                    if (runningContainers.isEmpty()) {
                        // Empty state for no running containers
                        Box(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(vertical = 32.dp),
                            contentAlignment = Alignment.Center
                        ) {
                            Column(
                                horizontalAlignment = Alignment.CenterHorizontally,
                                verticalArrangement = Arrangement.spacedBy(8.dp)
                            ) {
                                Icon(
                                    imageVector = Icons.Default.Dashboard,
                                    contentDescription = null,
                                    modifier = Modifier.size(48.dp),
                                    tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f)
                                )
                                Text(
                                    text = context.getString(R.string.no_containers_running),
                                    style = MaterialTheme.typography.bodyLarge,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                                Text(
                                    text = context.getString(R.string.start_container_first),
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
                                )
                            }
                        }
                    } else {
                        // Running container cards
                        runningContainers.forEach { container ->
                            RunningContainerCard(
                                container = container,
                                onEnter = {
                                    onNavigateToContainerDetails(container.name)
                                },
                                onTerminalClick = {
                                    onNavigateToToTerminal(container.name)
                                }
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

