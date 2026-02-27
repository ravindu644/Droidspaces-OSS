package com.droidspaces.app.ui.navigation

import androidx.compose.animation.*
import androidx.compose.animation.core.*
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import androidx.navigation.NavBackStackEntry
import androidx.navigation.NavHostController
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import com.droidspaces.app.util.PreferencesManager
import com.droidspaces.app.util.RootChecker
import com.droidspaces.app.util.RootStatus
import com.droidspaces.app.util.Constants
import com.droidspaces.app.util.AnimationUtils
import com.droidspaces.app.ui.screen.InstallationScreen
import com.droidspaces.app.ui.screen.MainTabScreen
import com.droidspaces.app.ui.screen.RootCheckScreen
import com.droidspaces.app.ui.screen.SettingsScreen
import com.droidspaces.app.ui.screen.RequirementsScreen
import com.droidspaces.app.ui.screen.WelcomeScreen
import com.droidspaces.app.ui.screen.ContainerNameScreen
import com.droidspaces.app.ui.screen.SparseImageConfigScreen
import com.droidspaces.app.ui.screen.ContainerConfigScreen
import com.droidspaces.app.ui.screen.InstallationSummaryScreen
import com.droidspaces.app.ui.screen.InstallationProgressScreen
import com.droidspaces.app.ui.screen.EditContainerScreen
import com.droidspaces.app.ui.screen.ContainerDetailsScreen
import com.droidspaces.app.ui.screen.ProcessListScreen
import com.droidspaces.app.ui.screen.SystemdScreen
import com.droidspaces.app.ui.viewmodel.ContainerInstallationViewModel
import com.droidspaces.app.ui.viewmodel.ContainerViewModel
import com.droidspaces.app.util.ContainerManager
import com.droidspaces.app.util.FilePickerUtils
import androidx.compose.ui.Alignment
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.ui.Modifier
import com.droidspaces.app.ui.util.LoadingIndicator
import com.droidspaces.app.ui.util.LoadingSize
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.activity.ComponentActivity
import android.net.Uri
import androidx.navigation.NavType
import androidx.navigation.navArgument
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * Sealed class representing all navigation destinations.
 * Simplified to remove skipRefresh parameter - state is now managed by ViewModel.
 */
sealed class Screen(val route: String) {
    data object Welcome : Screen("welcome")
    data object RootCheck : Screen("rootcheck")
    data object Installation : Screen("installation")
    data object Home : Screen("home?fromInstallation={fromInstallation}") {
        fun createRoute(fromInstallation: Boolean = false) = "home?fromInstallation=$fromInstallation"
    }
    data object Settings : Screen("settings")
    data object Requirements : Screen("requirements")

    // Container installation wizard screens
    data object ContainerName : Screen("container_name/{tarballUri}") {
        fun createRoute(tarballUri: String) = "container_name/${Uri.encode(tarballUri)}"
    }
    data object SparseImageConfig : Screen("sparse_image_config")
    data object ContainerConfig : Screen("container_config")
    data object InstallationSummary : Screen("installation_summary")
    data object InstallationProgress : Screen("installation_progress")

    // Container editing screen
    data object EditContainer : Screen("edit_container/{containerName}") {
        fun createRoute(containerName: String) = "edit_container/${Uri.encode(containerName)}"
    }

    // Container management screens
    data object ContainerDetails : Screen("container_details/{containerName}") {
        fun createRoute(containerName: String) = "container_details/${Uri.encode(containerName)}"
    }
    data object ProcessList : Screen("process_list/{containerName}") {
        fun createRoute(containerName: String) = "process_list/${Uri.encode(containerName)}"
    }
    data object Systemd : Screen("systemd/{containerName}") {
        fun createRoute(containerName: String) = "systemd/${Uri.encode(containerName)}"
    }
}

@Composable
fun DroidspacesNavigation(
    navController: NavHostController = rememberNavController(),
    onContentReady: () -> Unit = {}
) {
    val context = LocalContext.current
    val prefsManager = remember { PreferencesManager.getInstance(context) }

    // Shared ViewModel scoped to Activity to ensure state consistency across screens
    val activity = context as? ComponentActivity
    val sharedContainerViewModel: ContainerViewModel = if (activity != null) {
        viewModel(viewModelStoreOwner = activity)
    } else {
        viewModel()
    }

    // Determine initial destination immediately (synchronous read)
    val initialDestination = remember(prefsManager) {
        if (prefsManager.isSetupCompleted) Screen.Home.route else Screen.Welcome.route
    }

    var rootStatus by remember { mutableStateOf<RootStatus?>(null) }

    // Signal content ready and check root in background if needed
    LaunchedEffect(Unit) {
        onContentReady()
        if (prefsManager.isSetupCompleted && prefsManager.rootChecked) {
            launch(Dispatchers.IO) {
                rootStatus = RootChecker.checkRootAccess()
            }
        }
    }

    // Define animation specs - consistent 200ms for snappy feel
    val defaultEnterTransition: AnimatedContentTransitionScope<NavBackStackEntry>.() -> EnterTransition = {
        fadeIn(animationSpec = AnimationUtils.fastSpec())
    }
    val defaultExitTransition: AnimatedContentTransitionScope<NavBackStackEntry>.() -> ExitTransition = {
        fadeOut(animationSpec = AnimationUtils.fastSpec())
    }

    val setupEnterTransition: AnimatedContentTransitionScope<NavBackStackEntry>.() -> EnterTransition = {
        fadeIn(animationSpec = AnimationUtils.mediumSpec())
    }
    val setupExitTransition: AnimatedContentTransitionScope<NavBackStackEntry>.() -> ExitTransition = {
        fadeOut(animationSpec = AnimationUtils.mediumSpec())
    }

    NavHost(
        navController = navController,
        startDestination = initialDestination
    ) {
        composable(
            route = Screen.Welcome.route,
            enterTransition = setupEnterTransition,
            exitTransition = setupExitTransition
        ) {
            WelcomeScreen(
                onNavigateToRootCheck = {
                    navController.navigate(Screen.RootCheck.route)
                }
            )
        }

        composable(
            route = Screen.RootCheck.route,
            enterTransition = setupEnterTransition,
            exitTransition = setupExitTransition
        ) {
            RootCheckScreen(
                rootStatus = rootStatus,
                onRootCheck = { status ->
                    rootStatus = status
                    prefsManager.rootChecked = true
                },
                onNavigateToInstallation = {
                    navController.navigate(Screen.Installation.route)
                },
                onSkip = {
                    prefsManager.rootSkipped = true
                    prefsManager.isSetupCompleted = true
                    navController.navigate(Screen.Home.route) {
                        popUpTo(Screen.Welcome.route) { inclusive = true }
                    }
                }
            )
        }

        composable(
            route = Screen.Installation.route,
            enterTransition = setupEnterTransition,
            exitTransition = setupExitTransition
        ) {
            InstallationScreen(
                onInstallationComplete = {
                    prefsManager.isSetupCompleted = true

                    // Navigate to home with fromInstallation=true to trigger refresh
                    val isFromSetup = navController.previousBackStackEntry?.destination?.route == Screen.RootCheck.route
                    navController.navigate(Screen.Home.createRoute(fromInstallation = true)) {
                        if (isFromSetup) {
                            popUpTo(Screen.Welcome.route) { inclusive = true }
                        } else {
                            popUpTo(Screen.Home.route) { inclusive = true }
                        }
                    }
                }
            )
        }

        composable(
            route = Screen.Home.route,
            arguments = listOf(
                navArgument("fromInstallation") {
                    type = NavType.BoolType
                    defaultValue = false
                }
            ),
            enterTransition = defaultEnterTransition,
            exitTransition = defaultExitTransition,
            popEnterTransition = defaultEnterTransition,
            popExitTransition = defaultExitTransition
        ) { backStackEntry ->
            val fromInstallation = backStackEntry.arguments?.getBoolean("fromInstallation") ?: false

            MainTabScreen(
                containerViewModel = sharedContainerViewModel,
                // skipInitialRefresh=false when fromInstallation=true (invert the logic)
                skipInitialRefresh = !fromInstallation,
                onNavigateToSettings = {
                    navController.navigate(Screen.Settings.route)
                },
                onNavigateToInstallation = {
                    navController.navigate(Screen.Installation.route)
                },
                onNavigateToContainerInstallation = { uri ->
                    navController.navigate(Screen.ContainerName.createRoute(uri.toString()))
                },
                onNavigateToEditContainer = { containerName ->
                    navController.navigate(Screen.EditContainer.createRoute(containerName))
                },
                onNavigateToContainerDetails = { containerName ->
                    navController.navigate(Screen.ContainerDetails.createRoute(containerName))
                }
            )
        }

        // Container installation wizard screens
        composable(
            route = Screen.ContainerName.route,
            arguments = listOf(
                navArgument("tarballUri") { type = NavType.StringType }
            ),
            enterTransition = defaultEnterTransition,
            exitTransition = defaultExitTransition
        ) { backStackEntry ->
            val viewModel: ContainerInstallationViewModel = viewModel(backStackEntry)
            val tarballUriString = backStackEntry.arguments?.getString("tarballUri") ?: ""
            val tarballUri = Uri.parse(tarballUriString)

            LaunchedEffect(tarballUri) {
                viewModel.setTarball(tarballUri)
            }

            ContainerNameScreen(
                initialName = viewModel.containerName,
                initialHostname = viewModel.hostname,
                existingContainerNames = sharedContainerViewModel.containerList.map { it.name },
                onNext = { name, hostname ->
                    viewModel.setName(name, hostname)
                    navController.navigate(Screen.ContainerConfig.route)
                },
                onClose = {
                    navController.popBackStack()
                }
            )
        }

        composable(
            route = Screen.ContainerConfig.route,
            enterTransition = defaultEnterTransition,
            exitTransition = defaultExitTransition
        ) { backStackEntry ->
            val viewModel: ContainerInstallationViewModel = viewModel(
                remember(backStackEntry) {
                    runCatching { navController.getBackStackEntry(Screen.ContainerName.route) }
                        .getOrElse { backStackEntry }
                }
            )

            ContainerConfigScreen(
                initialEnableIPv6 = viewModel.enableIPv6,
                initialEnableAndroidStorage = viewModel.enableAndroidStorage,
                initialEnableHwAccess = viewModel.enableHwAccess,
                initialSelinuxPermissive = viewModel.selinuxPermissive,
                initialVolatileMode = viewModel.volatileMode,
                initialBindMounts = viewModel.bindMounts,
                initialDnsServers = viewModel.dnsServers,
                initialRunAtBoot = viewModel.runAtBoot,
                initialDisableSeccompFilter = viewModel.disableSeccompFilter,
                onNext = { enableIPv6, enableAndroidStorage, enableHwAccess, selinuxPermissive, volatileMode, bindMounts, dnsServers, runAtBoot, disableSeccompFilter ->
                    viewModel.setConfig(enableIPv6, enableAndroidStorage, enableHwAccess, selinuxPermissive, volatileMode, bindMounts, dnsServers, runAtBoot, disableSeccompFilter)
                    navController.navigate(Screen.SparseImageConfig.route)
                },
                onBack = {
                    navController.popBackStack()
                }
            )
        }

        composable(
            route = Screen.SparseImageConfig.route,
            enterTransition = defaultEnterTransition,
            exitTransition = defaultExitTransition
        ) { backStackEntry ->
            val viewModel: ContainerInstallationViewModel = viewModel(
                remember(backStackEntry) {
                    runCatching { navController.getBackStackEntry(Screen.ContainerName.route) }
                        .getOrElse { backStackEntry }
                }
            )

            SparseImageConfigScreen(
                initialUseSparseImage = viewModel.useSparseImage,
                initialSizeGB = viewModel.sparseImageSizeGB,
                onNext = { useSparseImage, sizeGB ->
                    viewModel.setSparseImageConfig(useSparseImage, sizeGB)
                    navController.navigate(Screen.InstallationSummary.route)
                },
                onBack = {
                    navController.popBackStack()
                }
            )
        }

        composable(
            route = Screen.InstallationSummary.route,
            enterTransition = defaultEnterTransition,
            exitTransition = defaultExitTransition
        ) { backStackEntry ->
            val viewModel: ContainerInstallationViewModel = viewModel(
                remember(backStackEntry) {
                    runCatching { navController.getBackStackEntry(Screen.ContainerName.route) }
                        .getOrElse { backStackEntry }
                }
            )
            val config = viewModel.buildConfig()
            val tarballUri = viewModel.tarballUri
            val ctx = LocalContext.current

            if (config != null && tarballUri != null) {
                // Extract filename from URI using FilePickerUtils (handles recent files properly)
                var tarballName by remember { mutableStateOf<String?>(null) }

                LaunchedEffect(tarballUri) {
                    tarballName = FilePickerUtils.getFileName(ctx, tarballUri) ?: "container.tar.gz"
                }

                // Show loading while extracting filename
                if (tarballName == null) {
                    Box(
                        modifier = Modifier.fillMaxSize(),
                        contentAlignment = Alignment.Center
                    ) {
                        LoadingIndicator(size = com.droidspaces.app.ui.util.LoadingSize.Large)
                    }
                } else {
                    InstallationSummaryScreen(
                        config = config,
                        tarballName = tarballName!!,
                        onInstall = {
                            navController.navigate(Screen.InstallationProgress.route)
                        },
                        onBack = {
                            navController.popBackStack()
                        }
                    )
                }
            } else {
                // Error state - navigate back
                LaunchedEffect(Unit) {
                    navController.popBackStack(Screen.Home.route, false)
                }
            }
        }

        composable(
            route = Screen.InstallationProgress.route,
            enterTransition = defaultEnterTransition,
            exitTransition = defaultExitTransition
        ) { backStackEntry ->
            val viewModel: ContainerInstallationViewModel = viewModel(
                remember(backStackEntry) {
                    runCatching { navController.getBackStackEntry(Screen.ContainerName.route) }
                        .getOrElse { backStackEntry }
                }
            )
            val config = viewModel.buildConfig()
            val tarballUri = viewModel.tarballUri

            if (config != null && tarballUri != null) {
                InstallationProgressScreen(
                    tarballUri = tarballUri,
                    config = config,
                    onSuccess = {
                        viewModel.reset()
                        // Trigger container list refresh before navigating back
                        sharedContainerViewModel.fetchContainerList()
                        navController.popBackStack(Screen.Home.route, false)
                    },
                    onError = {
                        navController.popBackStack(Screen.InstallationSummary.route, false)
                    }
                )
            } else {
                // Error state - navigate back
                LaunchedEffect(Unit) {
                    navController.popBackStack(Screen.Home.route, false)
                }
            }
        }

        composable(
            route = Screen.EditContainer.route,
            arguments = listOf(
                navArgument("containerName") { type = NavType.StringType }
            ),
            enterTransition = defaultEnterTransition,
            exitTransition = defaultExitTransition
        ) { backStackEntry ->
            val containerName = backStackEntry.arguments?.getString("containerName") ?: ""
            // Use shared ViewModel to ensure updates are reflected in other screens
            val containerViewModel: ContainerViewModel = sharedContainerViewModel

            var containerInfo by remember { mutableStateOf<com.droidspaces.app.util.ContainerInfo?>(null) }
            var isLoading by remember { mutableStateOf(true) }

            LaunchedEffect(containerName) {
                isLoading = true
                containerInfo = withContext(Dispatchers.IO) {
                    ContainerManager.getContainerInfo(containerName)
                }
                isLoading = false
            }

            if (isLoading) {
                Box(
                    modifier = Modifier.fillMaxSize(),
                    contentAlignment = Alignment.Center
                ) {
                    LoadingIndicator()
                }
            } else {
                containerInfo?.let { container ->
                    EditContainerScreen(
                        container = container,
                        containerViewModel = containerViewModel,
                        onBack = {
                            navController.popBackStack()
                        }
                    )
                } ?: LaunchedEffect(Unit) {
                    navController.popBackStack()
                }
            }
        }

        composable(
            route = Screen.Settings.route,
            enterTransition = defaultEnterTransition,
            exitTransition = defaultExitTransition,
            popEnterTransition = defaultEnterTransition,
            popExitTransition = defaultExitTransition
        ) {
            SettingsScreen(
                onBack = {
                    // Simply pop back - no refresh needed (ViewModel preserves state)
                    navController.popBackStack()
                },
                onNavigateToInstallation = {
                    navController.navigate(Screen.Installation.route)
                },
                onNavigateToRequirements = {
                    navController.navigate(Screen.Requirements.route)
                }
            )
        }

        composable(
            route = Screen.Requirements.route,
            enterTransition = defaultEnterTransition,
            exitTransition = defaultExitTransition,
            popEnterTransition = defaultEnterTransition,
            popExitTransition = defaultExitTransition
        ) {
            RequirementsScreen(
                onNavigateBack = {
                    navController.popBackStack()
                }
            )
        }

        composable(
            route = Screen.ContainerDetails.route,
            arguments = listOf(
                navArgument("containerName") { type = NavType.StringType }
            ),
            enterTransition = defaultEnterTransition,
            exitTransition = defaultExitTransition
        ) { backStackEntry ->
            val containerName = backStackEntry.arguments?.getString("containerName") ?: ""
            // Use shared ViewModel to ensure we see updated details
            val containerViewModel: ContainerViewModel = sharedContainerViewModel
            val container = containerViewModel.containerList.find { it.name == containerName }

            container?.let {
                ContainerDetailsScreen(
                    container = it,
                    onNavigateBack = { navController.popBackStack() },
                    onNavigateToSystemd = {
                        navController.navigate(Screen.Systemd.createRoute(containerName))
                    }
                )
            } ?: LaunchedEffect(Unit) {
                navController.popBackStack()
            }
        }

        composable(
            route = Screen.ProcessList.route,
            arguments = listOf(
                navArgument("containerName") { type = NavType.StringType }
            ),
            enterTransition = defaultEnterTransition,
            exitTransition = defaultExitTransition
        ) { backStackEntry ->
            val containerName = backStackEntry.arguments?.getString("containerName") ?: ""
            ProcessListScreen(
                containerName = containerName,
                onNavigateBack = { navController.popBackStack() }
            )
        }

        composable(
            route = Screen.Systemd.route,
            arguments = listOf(
                navArgument("containerName") { type = NavType.StringType }
            ),
            enterTransition = defaultEnterTransition,
            exitTransition = defaultExitTransition
        ) { backStackEntry ->
            val containerName = backStackEntry.arguments?.getString("containerName") ?: ""
            SystemdScreen(
                containerName = containerName,
                onNavigateBack = { navController.popBackStack() }
            )
        }
    }
}
