package com.droidspaces.app.ui.screen

import android.content.Context
import android.content.SharedPreferences
import android.content.ClipData
import android.content.ClipboardManager
import android.content.pm.PackageManager
import androidx.compose.foundation.clickable
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.BorderStroke
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.OpenInNew
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.material3.RadioButton
import androidx.compose.material.ripple.rememberRipple
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.painterResource
import android.content.Intent
import android.net.Uri
import com.droidspaces.app.R
import androidx.lifecycle.viewmodel.compose.viewModel
import com.droidspaces.app.ui.component.AccentColorPicker
import com.droidspaces.app.ui.component.BugReportDialog
import com.droidspaces.app.ui.component.SwitchItem
import com.droidspaces.app.ui.theme.ThemePalette
import com.droidspaces.app.ui.theme.rememberThemeState
import com.droidspaces.app.ui.viewmodel.AppStateViewModel
import com.droidspaces.app.util.PreferencesManager
import com.droidspaces.app.util.LocaleHelper
import com.droidspaces.app.util.ContributorManager
import com.droidspaces.app.util.Contributor
import android.graphics.BitmapFactory
import android.util.Base64
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.foundation.Image
import com.droidspaces.app.util.SymlinkInstaller
import androidx.core.content.edit
import java.util.Locale
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import androidx.compose.runtime.rememberCoroutineScope

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(
    onBack: () -> Unit,
    onNavigateToInstallation: () -> Unit = {},
    onNavigateToRequirements: () -> Unit = {},
    onNavigateToAutoBootPriority: () -> Unit = {}
) {
    val context = LocalContext.current
    val scrollBehavior = TopAppBarDefaults.pinnedScrollBehavior()
    val prefsManager = remember { PreferencesManager.getInstance(context) }
    val appStateViewModel: AppStateViewModel = viewModel()
    val isRootAvailable = appStateViewModel.isRootAvailable
    val scope = rememberCoroutineScope()

    // Theme state - use reactive theme state holder for instant updates
    // This eliminates redundant preference reads on every recomposition
    // ThemeStateHolder uses SharedPreferences listener for efficient updates
    val themeState = rememberThemeState()

    // Use theme state values directly - no redundant reads
    val followSystemTheme = themeState.followSystemTheme
    val darkTheme = themeState.darkTheme
    val amoledMode = themeState.amoledMode
    val useDynamicColor = themeState.useDynamicColor

    // About dialog state
    var showAboutDialog by remember { mutableStateOf(false) }

    // Bug Report dialog state
    var showBugReportDialog by remember { mutableStateOf(false) }

    // Language picker state
    var showLanguageDialog by remember { mutableStateOf(false) }
    var currentAppLocale by remember { mutableStateOf(LocaleHelper.getCurrentAppLocale(context)) }

    // Daemon mode state
    var isDaemonModeEnabled by remember { mutableStateOf(prefsManager.isDaemonModeEnabled) }

    // Symlink state
    var isSymlinkEnabled by remember { mutableStateOf(prefsManager.isSymlinkEnabled) }

    // Register SharedPreferences listener for daemon mode
    DisposableEffect(prefsManager) {
        val listener = SharedPreferences.OnSharedPreferenceChangeListener { _: SharedPreferences, key: String? ->
            when (key) {
                PreferencesManager.KEY_DAEMON_MODE_ENABLED -> isDaemonModeEnabled = prefsManager.isDaemonModeEnabled
                PreferencesManager.KEY_SYMLINK_ENABLED -> isSymlinkEnabled = prefsManager.isSymlinkEnabled
            }
        }
        prefsManager.prefs.registerOnSharedPreferenceChangeListener(listener)
        onDispose {
            prefsManager.prefs.unregisterOnSharedPreferenceChangeListener(listener)
        }
    }

    // Listen for locale changes and sync daemon mode from disk
    LaunchedEffect(Unit) {
        currentAppLocale = LocaleHelper.getCurrentAppLocale(context)
        withContext(Dispatchers.IO) {
            prefsManager.syncDaemonModeFromDisk()
            prefsManager.syncSymlinkFromDisk()
        }
    }

    Scaffold(
        containerColor = Color.Transparent,
        modifier = Modifier.nestedScroll(scrollBehavior.nestedScrollConnection),
        topBar = {
            TopAppBar(
                title = {
                    Text(
                        text = context.getString(R.string.settings),
                        style = MaterialTheme.typography.titleLarge,
                        fontWeight = FontWeight.Black
                    )
                },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(
                            imageVector = Icons.AutoMirrored.Filled.ArrowBack,
                            contentDescription = context.getString(R.string.back)
                        )
                    }
                },
                scrollBehavior = scrollBehavior,
                windowInsets = WindowInsets.safeDrawing.only(
                    WindowInsetsSides.Top + WindowInsetsSides.Horizontal
                )
            )
        },
        contentWindowInsets = WindowInsets(0)
    ) { innerPadding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
                .verticalScroll(rememberScrollState())
        ) {
            // Backend Reinstallation Section
            Text(
                text = context.getString(R.string.backend_section),
                style = MaterialTheme.typography.titleSmall,
                fontWeight = FontWeight.Bold,
                modifier = Modifier.padding(start = 24.dp, bottom = 8.dp, top = 8.dp),
                color = MaterialTheme.colorScheme.primary
            )

            Surface(
                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp),
                shape = RoundedCornerShape(24.dp),
                color = if (darkTheme) MaterialTheme.colorScheme.surfaceContainerHigh else MaterialTheme.colorScheme.surfaceContainer,
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f))
            ) {
                Column {
                    ListItem(colors = ListItemDefaults.colors(containerColor = Color.Transparent),
                        verticalAlignment = Alignment.Top,
                        leadingContent = {
                    Icon(
                        imageVector = Icons.Default.Build,
                        contentDescription = null,
                        tint = if (isRootAvailable) {
                            MaterialTheme.colorScheme.onSurface
                        } else {
                            MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f)
                        }
                    )
                },
                headlineContent = {
                    Text(
                        text = context.getString(R.string.reinstall_backend),
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.SemiBold,
                        color = if (isRootAvailable) {
                            MaterialTheme.colorScheme.onSurface
                        } else {
                            MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f)
                        }
                    )
                },
                supportingContent = {
                    Text(
                        text = context.getString(R.string.reinstall_backend_description),
                        color = if (isRootAvailable) {
                            MaterialTheme.colorScheme.onSurfaceVariant
                        } else {
                            MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.38f)
                        }
                    )
                },
                modifier = Modifier
                    .then(
                        if (isRootAvailable) {
                            Modifier.clickable {
                                onNavigateToInstallation()
                            }
                        } else {
                            Modifier
                        }
                    )
            )

            HorizontalDivider(modifier = Modifier.padding(horizontal = 16.dp), color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.3f))

            // Daemon Mode Toggle
            SwitchItem(
                icon = Icons.Default.SettingsBackupRestore,
                title = context.getString(R.string.daemon_mode),
                summary = context.getString(R.string.daemon_mode_description),
                checked = isDaemonModeEnabled,
                enabled = isRootAvailable,
                onCheckedChange = { checked ->
                    prefsManager.isDaemonModeEnabled = checked
                }
            )

            HorizontalDivider(modifier = Modifier.padding(horizontal = 16.dp), color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.3f))

            val isBackendAvailable = appStateViewModel.isBackendAvailable
            SwitchItem(
                icon = Icons.Default.Link,
                title = context.getString(R.string.symlink_integration),
                summary = context.getString(R.string.symlink_integration_description),
                checked = isSymlinkEnabled,
                enabled = isBackendAvailable,
                onCheckedChange = { checked ->
                    scope.launch {
                        val ok = withContext(Dispatchers.IO) {
                            if (checked) SymlinkInstaller.enable() else SymlinkInstaller.disable()
                        }
                        if (ok) {
                            prefsManager.isSymlinkEnabled = checked
                        }
                    }
                }
            )

            HorizontalDivider(modifier = Modifier.padding(horizontal = 16.dp), color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.3f))

            // Auto Boot Priority - clickable to navigate to the boot-order screen
            ListItem(colors = ListItemDefaults.colors(containerColor = Color.Transparent),
                verticalAlignment = Alignment.Top,
                leadingContent = {
                    Icon(
                        imageVector = Icons.Default.LowPriority,
                        contentDescription = null
                    )
                },
                headlineContent = {
                    Text(
                        text = context.getString(R.string.auto_boot_priority),
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.SemiBold
                    )
                },
                supportingContent = {
                    Text(context.getString(R.string.auto_boot_priority_description))
                },
                trailingContent = {
                    Icon(
                        imageVector = Icons.Default.ChevronRight,
                        contentDescription = null
                    )
                },
                modifier = Modifier.clickable { onNavigateToAutoBootPriority() }
            )

            HorizontalDivider(modifier = Modifier.padding(horizontal = 16.dp), color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.3f))

            // Requirements Card - clickable to navigate to requirements page
            RequirementsCard(
                onNavigateToRequirements = onNavigateToRequirements
            )
                }
            }

            Spacer(modifier = Modifier.height(8.dp))

            // Theme Section
            Text(
                text = context.getString(R.string.appearance_section),
                style = MaterialTheme.typography.titleSmall,
                fontWeight = FontWeight.Bold,
                modifier = Modifier.padding(start = 24.dp, bottom = 8.dp, top = 8.dp),
                color = MaterialTheme.colorScheme.primary
            )

            Surface(
                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp),
                shape = RoundedCornerShape(24.dp),
                color = if (darkTheme) MaterialTheme.colorScheme.surfaceContainerHigh else MaterialTheme.colorScheme.surfaceContainer,
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f))
            ) {
                Column {
                    // Language Picker
            val currentLanguageDisplay = remember(currentAppLocale) {
                val currentLanguageCode = LocaleHelper.getCurrentLanguageCode()
                if (currentLanguageCode == "system") {
                    context.getString(R.string.system_default)
                } else {
                    // Find the matching language from available languages to use the same name as in picker
                    val availableLanguages = LocaleHelper.getAvailableLanguages(context)
                    val matchingLanguage = availableLanguages.find { it.code == currentLanguageCode }
                    matchingLanguage?.nativeName ?: context.getString(R.string.system_default)
                }
            }

            ListItem(colors = ListItemDefaults.colors(containerColor = Color.Transparent),
                verticalAlignment = Alignment.Top,
                leadingContent = {
                    Icon(
                        imageVector = Icons.Default.Translate,
                        contentDescription = null
                    )
                },
                headlineContent = {
                    Text(
                        text = context.getString(R.string.language),
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.SemiBold
                    )
                },
                supportingContent = {
                    Text(currentLanguageDisplay)
                },
                modifier = Modifier.clickable {
                    showLanguageDialog = true
                }
            )

            HorizontalDivider(modifier = Modifier.padding(horizontal = 16.dp), color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.3f))

            // Follow System Theme
            SwitchItem(
                icon = Icons.Default.BrightnessAuto,
                title = context.getString(R.string.follow_system_theme),
                summary = context.getString(R.string.follow_system_theme_description),
                checked = followSystemTheme,
                onCheckedChange = { checked ->
                    prefsManager.followSystemTheme = checked
                }
            )

            // Dark Theme (only shown when not following system)
            if (!followSystemTheme) {
                HorizontalDivider(modifier = Modifier.padding(horizontal = 16.dp), color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.3f))
                SwitchItem(
                    icon = Icons.Default.DarkMode,
                    title = context.getString(R.string.dark_theme),
                    summary = context.getString(R.string.dark_theme_description),
                    checked = darkTheme,
                    onCheckedChange = { checked ->
                        prefsManager.darkTheme = checked
                    }
                )
            }

            // AMOLED Mode (shown when followSystemTheme is true OR manual darkTheme is true)
            if (followSystemTheme || darkTheme) {
                HorizontalDivider(modifier = Modifier.padding(horizontal = 16.dp), color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.3f))
                SwitchItem(
                    icon = Icons.Default.RadioButtonUnchecked,
                    title = context.getString(R.string.amoled_mode),
                    summary = context.getString(R.string.amoled_mode_description),
                    checked = amoledMode,
                    enabled = true,
                    onCheckedChange = { checked ->
                        prefsManager.amoledMode = checked
                        // Theme state updates automatically via SharedPreferences listener
                    }
                )
            }

            // Use Dynamic Color (Monet theming) - Only show on Android 12+
            if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.S) {
                HorizontalDivider(modifier = Modifier.padding(horizontal = 16.dp), color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.3f))
                SwitchItem(
                    icon = Icons.Default.ColorLens,
                    title = context.getString(R.string.dynamic_color),
                    summary = context.getString(R.string.dynamic_color_description),
                    checked = useDynamicColor,
                    onCheckedChange = { checked ->
                        prefsManager.useDynamicColor = checked
                        // Theme state updates automatically via SharedPreferences listener
                    }
                )
            }

            // Accent Color Picker - show when dynamic color is off,
            // or always show on devices below Android 12 (no dynamic color support)
            if (!useDynamicColor || android.os.Build.VERSION.SDK_INT < android.os.Build.VERSION_CODES.S) {
                HorizontalDivider(modifier = Modifier.padding(horizontal = 16.dp), color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.3f))
                AccentColorPicker(
                    selectedPalette = themeState.themePalette,
                    isDarkTheme = darkTheme,
                    onPaletteSelected = { palette ->
                        prefsManager.themePalette = palette.name
                    }
                )
            }
                }
            }

            Spacer(modifier = Modifier.height(8.dp))

            // Debugging Section
            Text(
                text = context.getString(R.string.debugging_section),
                style = MaterialTheme.typography.titleSmall,
                fontWeight = FontWeight.Bold,
                modifier = Modifier.padding(start = 24.dp, bottom = 8.dp, top = 8.dp),
                color = MaterialTheme.colorScheme.primary
            )

            Surface(
                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp),

                shape = RoundedCornerShape(24.dp),
                color = if (darkTheme) MaterialTheme.colorScheme.surfaceContainerHigh else MaterialTheme.colorScheme.surfaceContainer,
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f))
            ) {
                ListItem(colors = ListItemDefaults.colors(containerColor = Color.Transparent),
                    verticalAlignment = Alignment.Top,
                    leadingContent = {
                    Icon(
                        imageVector = Icons.Default.BugReport,
                        contentDescription = null,
                        tint = if (isRootAvailable) {
                            MaterialTheme.colorScheme.onSurface
                        } else {
                            MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f)
                        }
                    )
                },
                headlineContent = {
                    Text(
                        text = context.getString(R.string.generate_bug_report),
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.SemiBold,
                        color = if (isRootAvailable) {
                            MaterialTheme.colorScheme.onSurface
                        } else {
                            MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f)
                        }
                    )
                },
                supportingContent = {
                    Text(
                        text = context.getString(R.string.generate_bug_report_description),
                        color = if (isRootAvailable) {
                            MaterialTheme.colorScheme.onSurfaceVariant
                        } else {
                            MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.38f)
                        }
                    )
                },
                modifier = Modifier
                    .then(
                        if (isRootAvailable) {
                            Modifier.clickable {
                                showBugReportDialog = true
                            }
                        } else {
                            Modifier
                        }
                    )
                )
            }

            Spacer(modifier = Modifier.height(8.dp))

            // About Section
            Text(
                text = context.getString(R.string.about_section),
                style = MaterialTheme.typography.titleSmall,
                fontWeight = FontWeight.Bold,
                modifier = Modifier.padding(start = 24.dp, bottom = 8.dp, top = 8.dp),
                color = MaterialTheme.colorScheme.primary
            )

            Surface(
                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp),
                shape = RoundedCornerShape(24.dp),
                color = if (darkTheme) MaterialTheme.colorScheme.surfaceContainerHigh else MaterialTheme.colorScheme.surfaceContainer,
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f))
            ) {
                ListItem(colors = ListItemDefaults.colors(containerColor = Color.Transparent),
                    verticalAlignment = Alignment.Top,
                    leadingContent = {
                    Icon(
                        imageVector = Icons.Default.Info,
                        contentDescription = null
                    )
                },
                headlineContent = {
                    Text(
                        text = context.getString(R.string.about_droidspaces),
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.SemiBold
                    )
                },
                supportingContent = {
                    Text(getAppVersion(context))
                },
                modifier = Modifier.clickable {
                    showAboutDialog = true
                }
                )
            }

            Spacer(modifier = Modifier.height(24.dp))

        }
    }

    // About Dialog
    if (showAboutDialog) {
        AboutDialog(onDismiss = { showAboutDialog = false })
    }

    // Bug Report Dialog
    if (showBugReportDialog) {
        BugReportDialog(onDismiss = { showBugReportDialog = false })
    }

    // Language Picker Dialog
    if (showLanguageDialog) {
        LanguagePickerDialog(
            onDismiss = { showLanguageDialog = false },
            onLanguageSelected = { languageCode ->
                // Change language using modern AppCompatDelegate API
                // This automatically saves preference, recreates activity, and updates resources
                LocaleHelper.changeLanguage(languageCode)
                // Activity will recreate automatically, no need to call recreate()
            }
        )
    }
}

@Composable
private fun AboutDialog(onDismiss: () -> Unit) {
    val context = LocalContext.current
    val scrollState = rememberScrollState()

    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(
            usePlatformDefaultWidth = false
        )
    ) {
        Surface(
            modifier = Modifier
                .fillMaxWidth(0.9f)
                .fillMaxHeight(0.85f)
                .padding(16.dp),
            shape = RoundedCornerShape(20.dp),
            color = MaterialTheme.colorScheme.surfaceContainer,
            border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f))
        ) {
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(top = 24.dp, start = 24.dp, end = 24.dp, bottom = 8.dp)
            ) {
                // Title
                Text(
                    text = context.getString(R.string.app_name),
                    style = MaterialTheme.typography.headlineSmall,
                    fontWeight = FontWeight.Bold,
                    modifier = Modifier.padding(bottom = 16.dp)
                )

                // Scrollable content
                Column(
                    modifier = Modifier
                        .weight(1f)
                        .verticalScroll(scrollState),
                    verticalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                Text(
                    text = context.getString(R.string.about_description),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Text(
                    text = context.getString(R.string.developers),
                    style = MaterialTheme.typography.titleSmall,
                    fontWeight = FontWeight.SemiBold,
                    color = MaterialTheme.colorScheme.primary
                )
                Surface(
                    modifier = Modifier.fillMaxWidth(),
                    shape = RoundedCornerShape(24.dp),
                    color = MaterialTheme.colorScheme.surfaceContainerHigh,
                    border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f))
                ) {
                    Column {
                        // Developer 1
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .clickable {
                                    val intent = Intent(Intent.ACTION_VIEW, Uri.parse("https://github.com/ravindu644"))
                                    context.startActivity(intent)
                                }
                                .padding(horizontal = 16.dp, vertical = 12.dp),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(12.dp)
                        ) {
                            Icon(
                                imageVector = Icons.Default.Person,
                                contentDescription = null,
                                modifier = Modifier.size(24.dp),
                                tint = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                            Column(modifier = Modifier.weight(1f)) {
                                Text(
                                    text = context.getString(R.string.developer_ravindu644),
                                    style = MaterialTheme.typography.bodyMedium,
                                    fontWeight = FontWeight.Medium
                                )
                                Text(
                                    text = context.getString(R.string.developer_ravindu644_role),
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }
                            Icon(
                                imageVector = Icons.AutoMirrored.Filled.OpenInNew,
                                contentDescription = context.getString(R.string.github),
                                modifier = Modifier.size(18.dp),
                                tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
                            )
                        }

                        HorizontalDivider(modifier = Modifier.padding(horizontal = 16.dp), color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.3f))

                        // Telegram channel
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .clickable {
                                    val intent = Intent(Intent.ACTION_VIEW, Uri.parse("https://t.me/Droidspaces"))
                                    context.startActivity(intent)
                                }
                                .padding(horizontal = 16.dp, vertical = 12.dp),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(12.dp)
                        ) {
                            Icon(
                                painter = painterResource(id = R.drawable.ic_telegram),
                                contentDescription = null,
                                modifier = Modifier.size(24.dp),
                                tint = MaterialTheme.colorScheme.primary
                            )
                            Text(
                                text = context.getString(R.string.telegram_channel),
                                style = MaterialTheme.typography.bodyMedium,
                                fontWeight = FontWeight.Medium,
                                modifier = Modifier.weight(1f)
                            )
                            Icon(
                                imageVector = Icons.AutoMirrored.Filled.OpenInNew,
                                contentDescription = context.getString(R.string.telegram_channel),
                                modifier = Modifier.size(18.dp),
                                tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
                            )
                        }

                        HorizontalDivider(modifier = Modifier.padding(horizontal = 16.dp), color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.3f))

                        // Source Code row
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .clickable {
                                    val intent = Intent(Intent.ACTION_VIEW, Uri.parse("https://github.com/ravindu644/Droidspaces-OSS"))
                                    context.startActivity(intent)
                                }
                                .padding(horizontal = 16.dp, vertical = 12.dp),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(12.dp)
                        ) {
                            Icon(
                                imageVector = Icons.Default.Code,
                                contentDescription = null,
                                modifier = Modifier.size(24.dp),
                                tint = MaterialTheme.colorScheme.primary
                            )
                            Text(
                                text = context.getString(R.string.source_code),
                                style = MaterialTheme.typography.bodyMedium,
                                fontWeight = FontWeight.Medium,
                                modifier = Modifier.weight(1f)
                            )
                            Icon(
                                imageVector = Icons.AutoMirrored.Filled.OpenInNew,
                                contentDescription = context.getString(R.string.source_code),
                                modifier = Modifier.size(18.dp),
                                tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
                            )
                        }
                    }
                }

                // Contributors
                Text(
                    text = context.getString(R.string.contributors),
                    style = MaterialTheme.typography.titleSmall,
                    fontWeight = FontWeight.SemiBold,
                    color = MaterialTheme.colorScheme.primary
                )
                val contributors = remember { ContributorManager.load(context) }
                Surface(
                    modifier = Modifier.fillMaxWidth(),
                    shape = RoundedCornerShape(24.dp),
                    color = MaterialTheme.colorScheme.surfaceContainerHigh,
                    border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f))
                ) {
                    Column {
                        contributors.forEachIndexed { index, contributor ->
                            ContributorItem(
                                contributor = contributor,
                                onClick = {
                                    val intent = Intent(Intent.ACTION_VIEW, Uri.parse(contributor.githubUrl))
                                    context.startActivity(intent)
                                }
                            )
                            if (index < contributors.size - 1) {
                                HorizontalDivider(modifier = Modifier.padding(horizontal = 16.dp), color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.3f))
                            }
                        }
                    }
                }
                }

                // OK Button
                TextButton(
                    onClick = onDismiss,
                    modifier = Modifier.align(Alignment.End)
                ) {
                    Text(context.getString(R.string.ok))
                }
            }
        }
    }
}

@Composable
private fun ContributorItem(contributor: Contributor, onClick: () -> Unit) {
    val context = LocalContext.current
    val bitmap = remember(contributor.avatarB64) {
        if (contributor.avatarB64.isNotEmpty()) runCatching {
            val bytes = Base64.decode(contributor.avatarB64, Base64.DEFAULT)
            BitmapFactory.decodeByteArray(bytes, 0, bytes.size)?.asImageBitmap()
        }.getOrNull() else null
    }
    Row(
        modifier = Modifier.fillMaxWidth().clickable(onClick = onClick).padding(horizontal = 16.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        if (bitmap != null) {
            Image(
                bitmap = bitmap,
                contentDescription = null,
                modifier = Modifier.size(28.dp).clip(RoundedCornerShape(14.dp))
            )
        } else {
            Icon(Icons.Default.Person, contentDescription = null,
                modifier = Modifier.size(28.dp),
                tint = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        Column(modifier = Modifier.weight(1f)) {
            Text(contributor.login, style = MaterialTheme.typography.bodyMedium, fontWeight = FontWeight.Medium)
            Text(context.resources.getQuantityString(R.plurals.commit_count, contributor.commits, contributor.commits), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        Icon(Icons.AutoMirrored.Filled.OpenInNew,
            contentDescription = context.getString(R.string.github),
            modifier = Modifier.size(18.dp),
            tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f))
    }
}

private fun getAppVersion(context: Context): String {
    return try {
        val packageInfo = context.packageManager.getPackageInfo(context.packageName, 0)
        packageInfo.versionName ?: context.getString(R.string.unknown)
    } catch (e: PackageManager.NameNotFoundException) {
        context.getString(R.string.unknown)
    }
}

private fun getAppVersionCode(context: Context): Int {
    return try {
        val packageInfo = context.packageManager.getPackageInfo(context.packageName, 0)
        @Suppress("DEPRECATION")
        packageInfo.versionCode
    } catch (e: PackageManager.NameNotFoundException) {
        0
    }
}

/**
 * Requirements Card - Clickable card that navigates to requirements page
 */
@Composable
private fun RequirementsCard(
    onNavigateToRequirements: () -> Unit
) {
    val context = LocalContext.current

    ListItem(colors = ListItemDefaults.colors(containerColor = Color.Transparent),
        verticalAlignment = Alignment.Top,
        leadingContent = {
            Icon(
                imageVector = Icons.Default.Code,
                contentDescription = null
            )
        },
        headlineContent = {
            Text(
                text = context.getString(R.string.requirements),
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.SemiBold
            )
        },
        supportingContent = {
            Text(
                text = context.getString(R.string.requirements_for_droidspaces),
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        },
        trailingContent = {
            Icon(
                imageVector = Icons.Default.ChevronRight,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.onSurfaceVariant
            )
        },
        modifier = Modifier.clickable(onClick = onNavigateToRequirements)
    )
}

/**
 * Language Picker Dialog - Modern implementation using AppCompatDelegate.setApplicationLocales()
 * Works on ALL Android versions (API 24+) and ALL devices including Samsung One UI 8+
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun LanguagePickerDialog(
    onDismiss: () -> Unit,
    onLanguageSelected: (String) -> Unit
) {
    val context = LocalContext.current
    val availableLanguages = remember { LocaleHelper.getAvailableLanguages(context) }
    val allOptions = remember(availableLanguages) {
        buildList {
            add("system" to context.getString(R.string.system_default))
            availableLanguages.forEach { language -> add(language.code to language.nativeName) }
        }
    }
    val currentLanguage = remember { LocaleHelper.getCurrentLanguageCode() }
    var selectedIndex by remember {
        mutableIntStateOf(allOptions.indexOfFirst { (code, _) -> code == currentLanguage }.takeIf { it >= 0 } ?: 0)
    }

    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false)
    ) {
        Surface(
            modifier = Modifier
                .fillMaxWidth(0.92f)
                .wrapContentHeight()
                .padding(vertical = 24.dp),
            shape = RoundedCornerShape(24.dp),
            color = MaterialTheme.colorScheme.surfaceContainer,
            border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.4f)),
            tonalElevation = 0.dp
        ) {
            Column(modifier = Modifier.padding(24.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
                Text(
                    text = context.getString(R.string.language),
                    style = MaterialTheme.typography.titleLarge,
                    fontWeight = FontWeight.Bold
                )

                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .heightIn(max = 400.dp)
                        .verticalScroll(rememberScrollState()),
                    verticalArrangement = Arrangement.spacedBy(4.dp)
                ) {
                    allOptions.forEachIndexed { index, (_, displayName) ->
                        val isSelected = selectedIndex == index
                        Surface(
                            modifier = Modifier
                                .fillMaxWidth()
                                .clip(RoundedCornerShape(12.dp))
                                .clickable { selectedIndex = index },
                            color = if (isSelected) MaterialTheme.colorScheme.primary.copy(alpha = 0.08f) else Color.Transparent,
                            shape = RoundedCornerShape(12.dp),
                            tonalElevation = 0.dp
                        ) {
                            Row(
                                modifier = Modifier.padding(horizontal = 12.dp, vertical = 12.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                RadioButton(
                                    selected = isSelected,
                                    onClick = { selectedIndex = index },
                                    colors = RadioButtonDefaults.colors(
                                        selectedColor = MaterialTheme.colorScheme.primary,
                                        unselectedColor = MaterialTheme.colorScheme.outline
                                    )
                                )
                                Spacer(modifier = Modifier.width(8.dp))
                                Text(
                                    text = displayName,
                                    style = MaterialTheme.typography.bodyLarge,
                                    fontWeight = if (isSelected) FontWeight.SemiBold else FontWeight.Normal,
                                    color = if (isSelected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurface
                                )
                            }
                        }
                    }
                }

                Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                    val btnShape = RoundedCornerShape(14.dp)
                    // Cancel - follow standard dialog cancel style
                    Surface(
                        modifier = Modifier.weight(1f).height(48.dp).clip(btnShape).clickable(onClick = onDismiss),
                        shape = btnShape,
                        color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.06f),
                        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.4f)),
                        tonalElevation = 0.dp
                    ) {
                        Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                            Text(
                                context.getString(R.string.cancel),
                                style = MaterialTheme.typography.labelLarge,
                                fontWeight = FontWeight.SemiBold,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                    }
                    // OK - follow solid primary style from SparseSizeDialog
                    Surface(
                        modifier = Modifier.weight(1f).height(48.dp).clip(btnShape).clickable {
                            if (selectedIndex >= 0 && selectedIndex < allOptions.size) {
                                onLanguageSelected(allOptions[selectedIndex].first)
                            }
                            onDismiss()
                        },
                        shape = btnShape,
                        color = MaterialTheme.colorScheme.primary,
                        tonalElevation = 0.dp
                    ) {
                        Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                            Text(
                                context.getString(R.string.ok),
                                style = MaterialTheme.typography.labelLarge,
                                fontWeight = FontWeight.SemiBold,
                                color = MaterialTheme.colorScheme.onPrimary
                            )
                        }
                    }
                }
            }
        }
    }
}


