package com.droidspaces.app.ui.screen
import androidx.compose.ui.graphics.Color

import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import com.droidspaces.app.ui.component.PullToRefreshWrapper
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.activity.compose.BackHandler
import androidx.lifecycle.viewmodel.compose.viewModel
import com.droidspaces.app.ui.component.DroidspacesStatus
import com.droidspaces.app.ui.component.DroidspacesStatusCard
import com.droidspaces.app.ui.component.SystemInfoCard
import com.droidspaces.app.util.DroidspacesBackendStatus
import com.droidspaces.app.util.PreferencesManager
import com.droidspaces.app.util.SystemInfoManager
import com.droidspaces.app.ui.viewmodel.AppStateViewModel
import com.droidspaces.app.ui.viewmodel.ContainerViewModel
import com.droidspaces.app.ui.component.HelpCard
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import androidx.compose.ui.unit.sp
import com.droidspaces.app.R

enum class TabItem(val titleResId: Int, val icon: androidx.compose.ui.graphics.vector.ImageVector) {
    Home(R.string.home_title, Icons.Default.Home),
    Containers(R.string.containers, Icons.Default.Storage),
    ControlPanel(R.string.panel, Icons.Default.Dashboard)
}

/**
 * Main tab screen with optimized state management.
 *
 * Key improvements:
 * 1. Uses AppStateViewModel for backend status (persists across navigation)
 * 2. No redundant state variables (refreshCounter, shouldTriggerAnimatedRefresh removed)
 * 3. Single coroutine scope for all operations
 * 4. Proper recomposition boundaries
 *
 * This fixes the "settings back button glitch" by NOT re-checking backend on navigation.
 * Backend is only checked on:
 * 1. Cold app start
 * 2. Pull-to-refresh
 * 3. Post-installation (when returning from installation flow)
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MainTabScreen(
    containerViewModel: ContainerViewModel,
    skipInitialRefresh: Boolean = false,
    onNavigateToSettings: () -> Unit = {},
    onNavigateToInstallation: () -> Unit = {},
    onNavigateToContainerInstallation: (android.net.Uri) -> Unit = {},
    onNavigateToEditContainer: (String) -> Unit = {},
    onNavigateToContainerDetails: (String) -> Unit = {},
    onNavigateToTerminal: (String) -> Unit = {},
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()

    // ViewModels - persist across navigation (activity-scoped)
    val appStateViewModel: AppStateViewModel = viewModel()
    // containerViewModel is now passed as parameter to ensure sharing


    // Tab selection - survives configuration changes
    var selectedTab by rememberSaveable { mutableStateOf(TabItem.Home) }

    // Track which container has its action drawer expanded (hoisted for global collapse)
    var expandedContainerName by rememberSaveable { mutableStateOf<String?>(null) }

    // Handle back press - return to Home tab if not already there
    BackHandler(enabled = selectedTab != TabItem.Home) {
        selectedTab = TabItem.Home
    }

    // Track if we've already triggered initial load in this session
    var hasTriggeredInitialLoad by rememberSaveable { mutableStateOf(false) }

    // Initial setup - only runs ONCE per app session
    // DO NOT use hasTriggeredInitialLoad as a key, because changing it cancels the effect!
    LaunchedEffect(skipInitialRefresh) {
        if (!hasTriggeredInitialLoad) {
            hasTriggeredInitialLoad = true

            if (!skipInitialRefresh) {
                // Post-installation: reset and force refresh
                appStateViewModel.resetForPostInstallation()
            }

            // Always force check on initial boot
            appStateViewModel.checkBackendStatus(force = true)

            // Proactive recovery on first launch: Always run scan if backend available
            if (appStateViewModel.isBackendAvailable) {
                containerViewModel.runScan()
            } else {
                // If not available immediately, ensure we fetch when it does become available
                containerViewModel.fetchContainerList()
            }
        }
    }

    // Fetch containers when backend BECOMES available or evaluates to true
    LaunchedEffect(appStateViewModel.isBackendAvailable) {
        if (appStateViewModel.isBackendAvailable) {
            // Because runScan might be running, fetchContainerList is safe because it only
            // cancels previous fetch jobs, and runs concurrently (which is fine, UI populates fast).
            // We only trigger this if it's currently empty, to avoid double-fetching if runScan succeeded,
            // or just always run it since it's cheap and ensures sync.
            if (containerViewModel.containerList.isEmpty()) {
                containerViewModel.fetchContainerList()
            }
        }
    }

    // Map backend status to UI status - remember previous status to prevent glitches
    val currentBackendStatus = appStateViewModel.backendStatus
    val prefsManager = remember { PreferencesManager.getInstance(context) }

    // Initialize stable status from cached backend status to prevent initial boot glitch
    val stableDroidspacesStatus = remember {
        mutableStateOf<DroidspacesStatus?>(
            prefsManager.cachedBackendStatus?.let { cached ->
                when (cached) {
                    "UpdateAvailable" -> DroidspacesStatus.UpdateAvailable
                    "NotInstalled" -> DroidspacesStatus.NotInstalled
                    "Corrupted" -> DroidspacesStatus.Corrupted
                    "ModuleMissing" -> DroidspacesStatus.ModuleMissing
                    "" -> DroidspacesStatus.Working
                    else -> null
                }
            }
        )
    }

    // Track previous root status to detect when root becomes unavailable
    var previousRootAvailable by remember { mutableStateOf(appStateViewModel.isRootAvailable) }

    // Update stable status when backend status changes (but skip Checking to prevent flicker)
    LaunchedEffect(currentBackendStatus, appStateViewModel.isRootAvailable) {
        // Skip updates during Checking state to prevent flicker during refresh
        if (currentBackendStatus is DroidspacesBackendStatus.Checking) {
            return@LaunchedEffect
        }

        val newStatus = when (currentBackendStatus) {
            is DroidspacesBackendStatus.Available -> DroidspacesStatus.Working
            is DroidspacesBackendStatus.UpdateAvailable -> DroidspacesStatus.UpdateAvailable
            is DroidspacesBackendStatus.NotInstalled -> DroidspacesStatus.NotInstalled
            is DroidspacesBackendStatus.Corrupted -> DroidspacesStatus.Corrupted
            is DroidspacesBackendStatus.ModuleMissing -> DroidspacesStatus.ModuleMissing
            is DroidspacesBackendStatus.Checking -> return@LaunchedEffect // Already handled above
        }

        // Always update stable status when we have a non-Checking status
        // This allows updates from error to working (e.g., after installation)
        stableDroidspacesStatus.value = newStatus

        previousRootAvailable = appStateViewModel.isRootAvailable
    }

    // Use stable status if available, otherwise compute from current status
    val droidspacesStatus: DroidspacesStatus = stableDroidspacesStatus.value ?: when (currentBackendStatus) {
        is DroidspacesBackendStatus.Checking -> DroidspacesStatus.Working
        is DroidspacesBackendStatus.Available -> DroidspacesStatus.Working
        is DroidspacesBackendStatus.UpdateAvailable -> DroidspacesStatus.UpdateAvailable
        is DroidspacesBackendStatus.NotInstalled -> DroidspacesStatus.NotInstalled
        is DroidspacesBackendStatus.Corrupted -> DroidspacesStatus.Corrupted
        is DroidspacesBackendStatus.ModuleMissing -> DroidspacesStatus.ModuleMissing
    }

    // UI state from ViewModels
    val isBackendAvailable = appStateViewModel.isBackendAvailable
    // Only show checking on initial load when we don't have cached status
    val isChecking = !appStateViewModel.hasCompletedInitialCheck && stableDroidspacesStatus.value == null
    val containerCount = containerViewModel.containerCount
    val runningCount = containerViewModel.runningCount

    /**
     * Combined refresh function for pull-to-refresh.
     * Refreshes both backend status and container list with specialized logic per tab.
     */
    suspend fun performRefresh(tab: TabItem) {
        // Check root status first (in case user denied root access)
        appStateViewModel.checkRootStatus()
        // Then refresh backend status
        appStateViewModel.forceRefresh()
        // Force refresh droidspaces version to get latest after backend updates
        if (appStateViewModel.isBackendAvailable) {
            SystemInfoManager.refreshDroidspacesVersion(context)
            SystemInfoManager.refreshBackendMode(context)

            when (tab) {
                TabItem.Home, TabItem.Containers -> {
                    // Home/Containers: Always run a full scan on refresh for maximum visibility
                    containerViewModel.runScan()
                }
                TabItem.ControlPanel -> {
                    // Control Panel: snappy refresh, but background recovery
                    val rawList = withContext(Dispatchers.IO) {
                        com.droidspaces.app.util.ContainerManager.listContainers()
                    }
                    val anyRunning = rawList.any { it.isRunning }

                    if (!anyRunning) {
                        // Metadata missing for running containers? attempt foreground recovery
                        containerViewModel.runScan()
                    } else {
                        // Running containers found, update UI normally (snappy)
                        withContext(Dispatchers.Main) {
                            containerViewModel.updateState(rawList)
                        }
                        // Then scan in background silently to catch orphans
                        scope.launch {
                            containerViewModel.silentScan()
                        }
                    }
                }
            }
        }
    }

    Scaffold(
        containerColor = Color.Transparent,
        topBar = {
            TopAppBar(
                title = {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        modifier = Modifier.clickable(
                            interactionSource = remember { androidx.compose.foundation.interaction.MutableInteractionSource() },
                            indication = null
                        ) { expandedContainerName = null }
                    ) {
                        Icon(
                            imageVector = Icons.Default.Storage,
                            contentDescription = null,
                            modifier = Modifier
                                .padding(end = 8.dp)
                                .size(24.dp)
                        )
                        Text(
                            text = context.getString(selectedTab.titleResId),
                            style = MaterialTheme.typography.titleLarge,
                            fontWeight = FontWeight.Black
                        )
                    }
                },
                actions = {
                    IconButton(onClick = onNavigateToSettings) {
                        Icon(imageVector = Icons.Default.Settings, contentDescription = context.getString(R.string.settings))
                    }
                },
                windowInsets = WindowInsets.statusBars
            )
        },
        bottomBar = {
            // No bottom bar content here to avoid the solid background
        },
        contentWindowInsets = WindowInsets(0)
    ) { innerPadding ->
        Box(modifier = Modifier.fillMaxSize().padding(innerPadding)) {
            Column(
                modifier = Modifier
                    .fillMaxSize()
            ) {
                when (selectedTab) {
                    TabItem.Home -> {
                        HomeTabContent(
                            droidspacesStatus = droidspacesStatus,
                            isChecking = isChecking,
                            isRootAvailable = appStateViewModel.isRootAvailable,
                            onNavigateToInstallation = onNavigateToInstallation,
                            onNavigateToContainers = { selectedTab = TabItem.Containers },
                            onNavigateToControlPanel = { selectedTab = TabItem.ControlPanel },
                            containerCount = containerCount,
                            runningCount = runningCount,
                            onRefresh = { performRefresh(TabItem.Home) }
                        )
                    }

                    TabItem.Containers -> {
                        ContainersTabContent(
                            isBackendAvailable = isBackendAvailable,
                            isRootAvailable = appStateViewModel.isRootAvailable,
                            onNavigateToInstallation = onNavigateToContainerInstallation,
                            onNavigateToEditContainer = onNavigateToEditContainer,
                            onNavigateToContainerDetails = onNavigateToContainerDetails,
                            containerViewModel = containerViewModel,
                            onRefresh = { performRefresh(TabItem.Containers) },
                            expandedContainerName = expandedContainerName,
                            onExpandedContainerNameChange = { expandedContainerName = it }
                        )
                    }

                    TabItem.ControlPanel -> {
                        ControlPanelTabContent(
                            isBackendAvailable = isBackendAvailable,
                            isRootAvailable = appStateViewModel.isRootAvailable,
                            containerViewModel = containerViewModel,
                            onRefresh = { performRefresh(TabItem.ControlPanel) },
                            onNavigateToContainerDetails = onNavigateToContainerDetails,
                            onNavigateToTerminal = onNavigateToTerminal
                        )
                    }
                }
            }

            // Floating Bottom Bar Overlay
            Box(
                modifier = Modifier
                    .align(Alignment.BottomCenter)
                    .fillMaxWidth()
            ) {
                MainBottomBar(
                    selectedTab = selectedTab,
                    onTabSelected = { 
                        selectedTab = it
                        expandedContainerName = null
                    }
                )
            }
        }
    }
}

@Composable
private fun HomeTabContent(
    droidspacesStatus: DroidspacesStatus,
    isChecking: Boolean,
    isRootAvailable: Boolean,
    onNavigateToInstallation: () -> Unit,
    onNavigateToContainers: () -> Unit,
    onNavigateToControlPanel: () -> Unit,
    containerCount: Int,
    runningCount: Int,
    onRefresh: suspend () -> Unit
) {
    val context = LocalContext.current
    // Track refresh trigger for SystemInfoCard
    var refreshTrigger by remember { mutableStateOf(0) }

    PullToRefreshWrapper(
        onRefresh = {
            onRefresh()
            refreshTrigger++
        }
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 16.dp)
                .padding(top = 8.dp, bottom = 120.dp) // Large bottom padding for floating bar + FAB
        ) {
            DroidspacesStatusCard(
                status = droidspacesStatus,
                version = null,
                isChecking = isChecking,
                isRootAvailable = isRootAvailable,
                refreshTrigger = refreshTrigger,
                onClick = {
                    if (!isRootAvailable) {
                        // Disabled for non-root users
                        return@DroidspacesStatusCard
                    }
                    if (droidspacesStatus == DroidspacesStatus.NotInstalled ||
                        droidspacesStatus == DroidspacesStatus.Corrupted ||
                        droidspacesStatus == DroidspacesStatus.UpdateAvailable ||
                        droidspacesStatus == DroidspacesStatus.ModuleMissing
                    ) {
                        onNavigateToInstallation()
                    }
                }
            )

            Spacer(modifier = Modifier.height(16.dp))

            // Only show container and running count cards if root is available
            if (isRootAvailable) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(16.dp)
                ) {
                    // Container count card
                    Surface(
                        onClick = onNavigateToContainers,
                        modifier = Modifier
                            .weight(1f)
                            .height(140.dp),
                        shape = RoundedCornerShape(24.dp), // Slightly more rounded for home cards
                        color = MaterialTheme.colorScheme.surfaceContainer,
                        border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f))
                    ) {
                        Column(
                            modifier = Modifier
                                .fillMaxSize()
                                .padding(16.dp),
                            horizontalAlignment = Alignment.Start,
                            verticalArrangement = Arrangement.SpaceBetween
                        ) {
                            Icon(
                                imageVector = Icons.Default.Layers,
                                contentDescription = null,
                                tint = MaterialTheme.colorScheme.primary,
                                modifier = Modifier.size(28.dp)
                            )
                            Column {
                                Text(
                                    text = containerCount.toString(),
                                    style = MaterialTheme.typography.headlineMedium,
                                    fontWeight = FontWeight.Black
                                )
                                Text(
                                    text = context.getString(R.string.containers).uppercase(),
                                    style = MaterialTheme.typography.labelMedium,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f),
                                    letterSpacing = 1.sp
                                )
                            }
                        }
                    }

                    // Running count card
                    Surface(
                        onClick = onNavigateToControlPanel,
                        modifier = Modifier
                            .weight(1f)
                            .height(140.dp),
                        shape = RoundedCornerShape(24.dp),
                        color = MaterialTheme.colorScheme.surfaceContainer,
                        border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f))
                    ) {
                        Column(
                            modifier = Modifier
                                .fillMaxSize()
                                .padding(16.dp),
                            horizontalAlignment = Alignment.Start,
                            verticalArrangement = Arrangement.SpaceBetween
                        ) {
                            Icon(
                                imageVector = Icons.Default.PlayCircle,
                                contentDescription = null,
                                tint = MaterialTheme.colorScheme.secondary,
                                modifier = Modifier.size(28.dp)
                            )
                            Column {
                                Text(
                                    text = runningCount.toString(),
                                    style = MaterialTheme.typography.headlineMedium,
                                    fontWeight = FontWeight.Black
                                )
                                Text(
                                    text = context.getString(R.string.running).uppercase(),
                                    style = MaterialTheme.typography.labelMedium,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f),
                                    letterSpacing = 1.sp
                                )
                            }
                        }
                    }
                }

                Spacer(modifier = Modifier.height(6.dp))
            }

            SystemInfoCard(refreshTrigger = refreshTrigger)

            Spacer(modifier = Modifier.height(16.dp))

            HelpCard()

            Spacer(modifier = Modifier.height(16.dp))
        }
    }
}

@Composable
private fun ContainersTabContent(
    isBackendAvailable: Boolean,
    isRootAvailable: Boolean,
    onNavigateToInstallation: (android.net.Uri) -> Unit,
    onNavigateToEditContainer: (String) -> Unit,
    onNavigateToContainerDetails: (String) -> Unit,
    containerViewModel: ContainerViewModel,
    onRefresh: suspend () -> Unit,
    expandedContainerName: String?,
    onExpandedContainerNameChange: (String?) -> Unit
) {
    PullToRefreshWrapper(onRefresh = { onRefresh() }) {
        ContainersScreen(
            isBackendAvailable = isBackendAvailable,
            isRootAvailable = isRootAvailable,
            onNavigateToInstallation = onNavigateToInstallation,
            onNavigateToEditContainer = onNavigateToEditContainer,
            onNavigateToContainerDetails = onNavigateToContainerDetails,
            containerViewModel = containerViewModel,
            expandedContainerName = expandedContainerName,
            onExpandedContainerNameChange = onExpandedContainerNameChange
        )
    }
}

@Composable
private fun ControlPanelTabContent(
    isBackendAvailable: Boolean,
    isRootAvailable: Boolean,
    containerViewModel: ContainerViewModel,
    onRefresh: suspend () -> Unit,
    onNavigateToContainerDetails: (String) -> Unit,
    onNavigateToTerminal: (String) -> Unit,
) {
    var refreshTrigger by remember { mutableStateOf(0) }

    PullToRefreshWrapper(onRefresh = {
        onRefresh()
        refreshTrigger++
    }) {
        ControlPanelScreen(
            isBackendAvailable = isBackendAvailable,
            isRootAvailable = isRootAvailable,
            containerViewModel = containerViewModel,
            onNavigateToContainerDetails = onNavigateToContainerDetails,
            onNavigateToTerminal = onNavigateToTerminal,
            refreshTrigger = refreshTrigger
        )
    }
}

@Composable
private fun MainBottomBar(
    selectedTab: TabItem,
    onTabSelected: (TabItem) -> Unit
) {
    val context = LocalContext.current
    val tabs = TabItem.entries
    val selectedIndex = tabs.indexOf(selectedTab)
    
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = androidx.compose.ui.graphics.RectangleShape,
        color = MaterialTheme.colorScheme.surfaceContainer,
        shadowElevation = 0.dp 
    ) {
        Column(modifier = Modifier.fillMaxWidth()) {
            HorizontalDivider(
                color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.25f),
                thickness = 1.dp
            )
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 12.dp, vertical = 8.dp)
                    .height(56.dp)
            ) {
                // Background Indicator
                BoxWithConstraints(modifier = Modifier.fillMaxWidth()) {
                    val tabWidth = maxWidth / tabs.size
                    val offset by androidx.compose.animation.core.animateDpAsState(
                        targetValue = tabWidth * selectedIndex,
                        animationSpec = androidx.compose.animation.core.spring(
                            dampingRatio = androidx.compose.animation.core.Spring.DampingRatioLowBouncy,
                            stiffness = androidx.compose.animation.core.Spring.StiffnessLow
                        ),
                        label = "IndicatorOffset"
                    )

                    Surface(
                        modifier = Modifier
                            .width(tabWidth)
                            .fillMaxHeight()
                            .offset(x = offset),
                        shape = RoundedCornerShape(18.dp),
                        color = MaterialTheme.colorScheme.primary.copy(alpha = 0.12f)
                    ) {}
                }

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceEvenly,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    tabs.forEach { tab ->
                        val isSelected = selectedTab == tab
                        val contentColor by androidx.compose.animation.animateColorAsState(
                            targetValue = if (isSelected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f),
                            label = "IconColor"
                        )

                        Surface(
                            onClick = { onTabSelected(tab) },
                            modifier = Modifier
                                .weight(1f)
                                .fillMaxHeight(),
                            color = androidx.compose.ui.graphics.Color.Transparent,
                            shape = RoundedCornerShape(16.dp)
                        ) {
                            Column(
                                modifier = Modifier.fillMaxSize(),
                                horizontalAlignment = Alignment.CenterHorizontally,
                                verticalArrangement = Arrangement.Center
                            ) {
                                Icon(
                                    imageVector = tab.icon,
                                    contentDescription = null,
                                    modifier = Modifier.size(if (isSelected) 24.dp else 22.dp),
                                    tint = contentColor
                                )
                                Text(
                                    text = context.getString(tab.titleResId),
                                    style = MaterialTheme.typography.labelSmall,
                                    color = contentColor,
                                    fontWeight = if (isSelected) FontWeight.Bold else FontWeight.Normal,
                                    fontSize = if (isSelected) 11.sp else 10.sp
                                )
                            }
                        }
                    }
                }
            }
            Spacer(modifier = Modifier.navigationBarsPadding())
        }
    }
}
