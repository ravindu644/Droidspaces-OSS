package com.droidspaces.app.ui.screen
import android.content.Intent
import android.net.Uri
import android.widget.Toast
import androidx.compose.ui.graphics.Color

import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import com.droidspaces.app.ui.component.PullToRefreshWrapper
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.activity.compose.BackHandler
import com.droidspaces.app.ui.component.DroidspacesStatus
import com.droidspaces.app.ui.component.DroidspacesStatusCard
import com.droidspaces.app.ui.component.SystemInfoCard
import com.droidspaces.app.util.DroidspacesBackendStatus
import com.droidspaces.app.util.SystemInfoManager
import com.droidspaces.app.ui.viewmodel.AppStateViewModel
import com.droidspaces.app.ui.viewmodel.ContainerViewModel
import com.droidspaces.app.ui.component.HelpCard
import com.droidspaces.app.util.AppUpdateInfo
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import androidx.compose.ui.unit.sp
import com.droidspaces.app.R
import com.droidspaces.app.ui.theme.DsIcons
import com.droidspaces.app.util.HostCapabilities
import androidx.compose.runtime.collectAsState
import com.droidspaces.app.util.Constants

private const val EASTER_EGG_URL = "https://www.youtube.com/watch?v=dQw4w9WgXcQ"

enum class TabItem(val titleResId: Int, val icon: androidx.compose.ui.graphics.vector.ImageVector) {
    Home(R.string.home_title, Icons.Default.Home),
    Containers(R.string.containers, DsIcons.Logo),
    ControlPanel(R.string.panel, Icons.Default.Dashboard)
}

/**
 * Backend is only checked on:
 * 1. Cold app start
 * 2. Pull-to-refresh
 * 3. Post-installation (when returning from installation flow)
 */
@OptIn(ExperimentalMaterial3Api::class, ExperimentalFoundationApi::class)
@Composable
fun MainTabScreen(
    appStateViewModel: AppStateViewModel,
    containerViewModel: ContainerViewModel,
    skipInitialRefresh: Boolean = false,
    requestedTab: TabItem? = null,
    onRequestedTabConsumed: () -> Unit = {},
    onNavigateToSettings: () -> Unit = {},
    onNavigateToInstallation: () -> Unit = {},
    onNavigateToContainerInstallation: (android.net.Uri) -> Unit = {},
    onNavigateToEditContainer: (String) -> Unit = {},
    onNavigateToContainerDetails: (String) -> Unit = {},
    onNavigateToTerminal: (String) -> Unit = {},
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()

    // containerViewModel is now passed as parameter to ensure sharing


    val tabs = remember { TabItem.values() }
    val pagerState = rememberPagerState(initialPage = 0, pageCount = { tabs.size })

    // Read once here and handed down as a Boolean, so Home, Containers and the
    // Panel can never disagree. Unknown counts as supported until the probe lands.
    val caps by HostCapabilities.state.collectAsState()
    val isKernelSupported = caps?.requirementsMet != false
    val selectedTab = tabs[pagerState.currentPage]

    // Jump to the tab requested by a launcher shortcut, then clear the request.
    LaunchedEffect(requestedTab) {
        val target = requestedTab ?: return@LaunchedEffect
        val index = tabs.indexOf(target)
        if (index >= 0) {
            pagerState.scrollToPage(index)
        }
        onRequestedTabConsumed()
    }

    // Track which container has its action drawer expanded (hoisted for global collapse)
    var expandedContainerName by rememberSaveable { mutableStateOf<String?>(null) }

    // Handle back press - return to Home tab if not already there
    BackHandler(enabled = selectedTab != TabItem.Home) {
        scope.launch {
            pagerState.scrollToPage(tabs.indexOf(TabItem.Home))
        }
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
                containerViewModel.scan(showIndicator = true)
            } else {
                // If not available immediately, ensure we fetch when it does become available
                containerViewModel.fetchContainerList()
            }
        }
    }

    // Fetch containers when backend BECOMES available or evaluates to true
    LaunchedEffect(appStateViewModel.isBackendAvailable) {
        if (appStateViewModel.isBackendAvailable) {
            // Re-probe only after a reboot or a backend update, the cache covers the rest.
            HostCapabilities.refreshIfStale(context)
            // Because scan might be running, fetchContainerList is safe because it only
            // cancels previous fetch jobs, and runs concurrently (which is fine, UI populates fast).
            // We only trigger this if it's currently empty, to avoid double-fetching if scan succeeded,
            // or just always run it since it's cheap and ensures sync.
            if (containerViewModel.containerList.isEmpty()) {
                containerViewModel.fetchContainerList()
            }
        }
    }

    // The card keeps the last settled status through a Checking pass, which is what
    // stops it flickering during a refresh; the ViewModel already seeds that from the
    // cached status, so a cold start shows the right card at once.
    val currentBackendStatus = appStateViewModel.backendStatus
    var stableDroidspacesStatus by remember { mutableStateOf(currentBackendStatus.toUi()) }
    LaunchedEffect(currentBackendStatus) {
        currentBackendStatus.toUi()?.let { stableDroidspacesStatus = it }
    }
    val droidspacesStatus: DroidspacesStatus = stableDroidspacesStatus ?: DroidspacesStatus.Working

    // UI state from ViewModels
    val isBackendAvailable = appStateViewModel.isBackendAvailable
    // Only show checking on initial load when we don't have cached status
    val isChecking = !appStateViewModel.hasCompletedInitialCheck && stableDroidspacesStatus == null
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
        appStateViewModel.checkBackendStatus(force = true)
        // Force refresh droidspaces version to get latest after backend updates
        if (appStateViewModel.isBackendAvailable) {
            SystemInfoManager.getDroidspacesVersion(context, force = true)
            SystemInfoManager.getBackendMode(context, force = true)
            HostCapabilities.refresh(context)

            when (tab) {
                TabItem.Home, TabItem.Containers -> {
                    // Home/Containers: Always run a full scan on refresh for maximum visibility
                    containerViewModel.scan(showIndicator = true)
                }
                TabItem.ControlPanel -> {
                    // Control Panel: snappy refresh, but background recovery
                    val rawList = withContext(Dispatchers.IO) {
                        com.droidspaces.app.util.ContainerManager.listContainers()
                    }
                    val anyRunning = rawList.any { it.isRunning }

                    if (!anyRunning) {
                        // Metadata missing for running containers? attempt foreground recovery
                        containerViewModel.scan(showIndicator = true)
                    } else {
                        // Running containers found, update UI normally (snappy)
                        withContext(Dispatchers.Main) {
                            containerViewModel.updateState(rawList)
                        }
                        // Then scan in background silently to catch orphans
                        scope.launch {
                            containerViewModel.scan(showIndicator = false)
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
                            imageVector = DsIcons.Logo,
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
        val density = LocalDensity.current
        // Measured height of the floating bottom bar, system nav inset included.
        // The tabs draw under it, so anything pinned to their bottom edge offsets
        // by this. A fixed dp guess fits gesture nav and lands behind the bar on
        // three-button nav, whose 48dp inset is much taller.
        var bottomBarHeight by remember { mutableStateOf(0.dp) }
        Box(modifier = Modifier.fillMaxSize().padding(innerPadding)) {
            HorizontalPager(
                state = pagerState,
                modifier = Modifier.fillMaxSize()
            ) { page ->
                when (tabs[page]) {
                    TabItem.Home -> {
                        HomeTabContent(
                            appUpdate = appStateViewModel.appUpdate,
                            droidspacesStatus = droidspacesStatus,
                            isChecking = isChecking,
                            isRootAvailable = appStateViewModel.isRootAvailable,
                            isKernelSupported = isKernelSupported,
                            onNavigateToInstallation = onNavigateToInstallation,
                            onNavigateToContainers = {
                                scope.launch {
                                    pagerState.scrollToPage(tabs.indexOf(TabItem.Containers))
                                }
                            },
                            onNavigateToControlPanel = {
                                scope.launch {
                                    pagerState.scrollToPage(tabs.indexOf(TabItem.ControlPanel))
                                }
                            },
                            containerCount = containerCount,
                            runningCount = runningCount,
                            onRefresh = { performRefresh(TabItem.Home) },
                            bottomBarInset = bottomBarHeight
                        )
                    }

                    TabItem.Containers -> {
                        PullToRefreshWrapper(onRefresh = { performRefresh(TabItem.Containers) }) {
                            ContainersScreen(
                                isBackendAvailable = isBackendAvailable,
                                isRootAvailable = appStateViewModel.isRootAvailable,
                                isKernelSupported = isKernelSupported,
                                onNavigateToInstallation = onNavigateToContainerInstallation,
                                onNavigateToEditContainer = onNavigateToEditContainer,
                                onNavigateToContainerDetails = onNavigateToContainerDetails,
                                containerViewModel = containerViewModel,
                                expandedContainerName = expandedContainerName,
                                onExpandedContainerNameChange = { expandedContainerName = it },
                                bottomBarInset = bottomBarHeight
                            )
                        }
                    }

                    TabItem.ControlPanel -> {
                        PullToRefreshWrapper(onRefresh = { performRefresh(TabItem.ControlPanel) }) {
                            ControlPanelScreen(
                                isBackendAvailable = isBackendAvailable,
                                isRootAvailable = appStateViewModel.isRootAvailable,
                                isKernelSupported = isKernelSupported,
                                containerViewModel = containerViewModel,
                                onNavigateToContainerDetails = onNavigateToContainerDetails,
                                onNavigateToTerminal = onNavigateToTerminal,
                                bottomBarInset = bottomBarHeight
                            )
                        }
                    }
                }
            }

            // Floating Bottom Bar Overlay
            Box(
                modifier = Modifier
                    .align(Alignment.BottomCenter)
                    .fillMaxWidth()
                    .onSizeChanged { bottomBarHeight = with(density) { it.height.toDp() } }
            ) {
                MainBottomBar(
                    selectedTab = selectedTab,
                    onTabSelected = { tab ->
                        scope.launch {
                            pagerState.scrollToPage(tabs.indexOf(tab))
                        }
                        expandedContainerName = null
                    }
                )
            }
        }
    }
}

@Composable
private fun HomeTabContent(
    appUpdate: AppUpdateInfo?,
    droidspacesStatus: DroidspacesStatus,
    isChecking: Boolean,
    isRootAvailable: Boolean,
    isKernelSupported: Boolean,
    onNavigateToInstallation: () -> Unit,
    onNavigateToContainers: () -> Unit,
    onNavigateToControlPanel: () -> Unit,
    containerCount: Int,
    runningCount: Int,
    onRefresh: suspend () -> Unit,
    bottomBarInset: Dp = 0.dp
) {
    val context = LocalContext.current
    // Track refresh trigger for SystemInfoCard
    var refreshTrigger by remember { mutableStateOf(0) }

    // Taps on a healthy status card do nothing useful, so count them for the
    // easter egg. Reset whenever the backend state changes so a tap that was
    // meant for the installer never lands on YouTube instead.
    var idleTaps by remember(droidspacesStatus) { mutableStateOf(0) }

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
                .padding(top = 8.dp, bottom = bottomBarInset + 16.dp)
        ) {
            DroidspacesStatusCard(
                status = droidspacesStatus,
                version = null,
                isChecking = isChecking,
                isRootAvailable = isRootAvailable,
                refreshTrigger = refreshTrigger,
                appUpdate = appUpdate,
                isKernelSupported = isKernelSupported,
                onClick = {
                    if (!isRootAvailable) {
                        // Disabled for non-root users
                        return@DroidspacesStatusCard
                    }
                    if (droidspacesStatus == DroidspacesStatus.NotInstalled ||
                        droidspacesStatus == DroidspacesStatus.UpdateAvailable ||
                        droidspacesStatus == DroidspacesStatus.ModuleMissing
                    ) {
                        onNavigateToInstallation()
                        return@DroidspacesStatusCard
                    }
                    if (droidspacesStatus != DroidspacesStatus.Working) return@DroidspacesStatusCard
                    if (!isKernelSupported) {
                        runCatching {
                            context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(Constants.KERNEL_CONFIG_DOC_URL)))
                        }
                        return@DroidspacesStatusCard
                    }
                    idleTaps++
                    when (idleTaps) {
                        5 -> Toast.makeText(context, R.string.easter_egg_warning, Toast.LENGTH_SHORT).show()
                        10 -> {
                            idleTaps = 0
                            Toast.makeText(context, R.string.easter_egg_reward, Toast.LENGTH_SHORT).show()
                            runCatching {
                                context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(EASTER_EGG_URL)))
                            }
                        }
                    }
                }
            )

            Spacer(modifier = Modifier.height(16.dp))

            // The count cards lead to the other tabs, so hide them when those
            // tabs would only show the root or kernel state.
            if (isRootAvailable && isKernelSupported) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(16.dp)
                ) {
                    // Container count card
                    CountCard(Icons.Default.Layers, MaterialTheme.colorScheme.primary, containerCount, context.getString(R.string.containers), onNavigateToContainers, Modifier.weight(1f))

                    // Running count card
                    CountCard(Icons.Default.PlayCircle, MaterialTheme.colorScheme.secondary, runningCount, context.getString(R.string.running), onNavigateToControlPanel, Modifier.weight(1f))
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

/** One of the two 140dp count tiles on Home, leading to the tab it counts. */
@Composable
private fun CountCard(icon: ImageVector, tint: Color, count: Int, label: String, onClick: () -> Unit, modifier: Modifier = Modifier) {
    Surface(
        onClick = onClick,
        modifier = modifier.height(140.dp),
        shape = RoundedCornerShape(24.dp),
        color = MaterialTheme.colorScheme.surfaceContainer,
        border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f))
    ) {
        Column(
            modifier = Modifier.fillMaxSize().padding(16.dp),
            horizontalAlignment = Alignment.Start,
            verticalArrangement = Arrangement.SpaceBetween
        ) {
            Icon(icon, contentDescription = null, tint = tint, modifier = Modifier.size(28.dp))
            Column {
                Text(count.toString(), style = MaterialTheme.typography.headlineMedium, fontWeight = FontWeight.Black)
                Text(
                    label.uppercase(),
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f),
                    letterSpacing = 1.sp
                )
            }
        }
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
                                    fontSize = 11.sp
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

/** The home card's view of the backend; null for Checking, which keeps the last one. */
private fun DroidspacesBackendStatus.toUi(): DroidspacesStatus? = when (this) {
    DroidspacesBackendStatus.Checking -> null
    DroidspacesBackendStatus.Available -> DroidspacesStatus.Working
    DroidspacesBackendStatus.UpdateAvailable -> DroidspacesStatus.UpdateAvailable
    DroidspacesBackendStatus.NotInstalled -> DroidspacesStatus.NotInstalled
    DroidspacesBackendStatus.ModuleMissing -> DroidspacesStatus.ModuleMissing
}
