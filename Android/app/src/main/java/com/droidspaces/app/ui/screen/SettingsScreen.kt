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
import androidx.core.content.edit
import java.util.Locale
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(
    onBack: () -> Unit,
    onNavigateToInstallation: () -> Unit = {},
    onNavigateToRequirements: () -> Unit = {}
) {
    val context = LocalContext.current
    val scrollBehavior = TopAppBarDefaults.pinnedScrollBehavior()
    val prefsManager = remember { PreferencesManager.getInstance(context) }
    val appStateViewModel: AppStateViewModel = viewModel()
    val isRootAvailable = appStateViewModel.isRootAvailable

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

    // Register SharedPreferences listener for daemon mode
    DisposableEffect(prefsManager) {
        val listener = SharedPreferences.OnSharedPreferenceChangeListener { _: SharedPreferences, key: String? ->
            if (key == PreferencesManager.KEY_DAEMON_MODE_ENABLED) {
                isDaemonModeEnabled = prefsManager.isDaemonModeEnabled
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
        }
    }

    Scaffold(
        modifier = Modifier.nestedScroll(scrollBehavior.nestedScrollConnection),
        topBar = {
            TopAppBar(
                title = {
                    Text(
                        text = context.getString(R.string.settings_title),
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
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.SemiBold,
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 16.dp),
                color = MaterialTheme.colorScheme.primary
            )

            ListItem(
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

            // Requirements Card - clickable to navigate to requirements page
            RequirementsCard(
                onNavigateToRequirements = onNavigateToRequirements
            )

            HorizontalDivider(modifier = Modifier.padding(vertical = 8.dp))

            // Theme Section
            Text(
                text = context.getString(R.string.appearance_section),
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.SemiBold,
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 16.dp),
                color = MaterialTheme.colorScheme.primary
            )

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

            ListItem(
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

            // Follow System Theme
            SwitchItem(
                icon = Icons.Default.BrightnessAuto,
                title = context.getString(R.string.follow_system_theme),
                summary = context.getString(R.string.follow_system_theme_description),
                checked = followSystemTheme,
                onCheckedChange = { checked ->
                    prefsManager.followSystemTheme = checked
                    // Disable AMOLED mode when enabling follow system theme
                    // Theme state updates automatically via SharedPreferences listener
                    if (checked && amoledMode) {
                        prefsManager.amoledMode = false
                    }
                }
            )

            // Dark Theme (only shown when not following system)
            if (!followSystemTheme) {
                SwitchItem(
                    icon = Icons.Default.DarkMode,
                    title = context.getString(R.string.dark_theme),
                    summary = context.getString(R.string.dark_theme_description),
                    checked = darkTheme,
                    onCheckedChange = { checked ->
                        prefsManager.darkTheme = checked
                        // Disable AMOLED if dark theme is disabled
                        // Theme state updates automatically via SharedPreferences listener
                        if (!checked && amoledMode) {
                            prefsManager.amoledMode = false
                        }
                    }
                )

                // AMOLED Mode (only shown when dark theme is explicitly enabled, directly beneath Dark Theme)
                if (darkTheme) {
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
            }

            // Use Dynamic Color (Monet theming) - Only show on Android 12+
            if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.S) {
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
                AccentColorPicker(
                    selectedPalette = themeState.themePalette,
                    isDarkTheme = darkTheme,
                    onPaletteSelected = { palette ->
                        prefsManager.themePalette = palette.name
                    }
                )
            }

            HorizontalDivider(modifier = Modifier.padding(vertical = 8.dp))

            // Bug Report Section
            ListItem(
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

            HorizontalDivider(modifier = Modifier.padding(vertical = 8.dp))

            // About Section
            Text(
                text = context.getString(R.string.about_section),
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.SemiBold,
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 16.dp),
                color = MaterialTheme.colorScheme.primary
            )

            ListItem(
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

            Spacer(modifier = Modifier.height(16.dp))

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
        Card(
            modifier = Modifier
                .fillMaxWidth(0.9f)
                .fillMaxHeight(0.85f)
                .padding(16.dp),
            shape = RoundedCornerShape(28.dp),
            colors = CardDefaults.cardColors(
                containerColor = MaterialTheme.colorScheme.surface
            )
        ) {
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(24.dp)
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
                Spacer(modifier = Modifier.height(8.dp))
                HorizontalDivider()
                Spacer(modifier = Modifier.height(4.dp))
                Text(
                    text = context.getString(R.string.developers),
                    style = MaterialTheme.typography.titleSmall,
                    fontWeight = FontWeight.SemiBold,
                    color = MaterialTheme.colorScheme.primary
                )
                // Developer 1
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clickable {
                            val intent = Intent(Intent.ACTION_VIEW, Uri.parse("https://github.com/ravindu644"))
                            context.startActivity(intent)
                        },
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Icon(
                        imageVector = Icons.Default.Person,
                        contentDescription = null,
                        modifier = Modifier.size(20.dp),
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
                // Developer 2
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clickable {
                            val intent = Intent(Intent.ACTION_VIEW, Uri.parse("https://github.com/thesithumsandaruwan"))
                            context.startActivity(intent)
                        },
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Icon(
                        imageVector = Icons.Default.Person,
                        contentDescription = null,
                        modifier = Modifier.size(20.dp),
                        tint = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Column(modifier = Modifier.weight(1f)) {
                        Text(
                            text = context.getString(R.string.developer_thesithumsandaruwan),
                            style = MaterialTheme.typography.bodyMedium,
                            fontWeight = FontWeight.Medium
                        )
                        Text(
                            text = context.getString(R.string.developer_thesithumsandaruwan_role),
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
                Spacer(modifier = Modifier.height(4.dp))
                HorizontalDivider()
                Spacer(modifier = Modifier.height(4.dp))
                // Telegram channel
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clickable {
                            val intent = Intent(Intent.ACTION_VIEW, Uri.parse("https://t.me/Droidspaces"))
                            context.startActivity(intent)
                        },
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
                Spacer(modifier = Modifier.height(4.dp))
                HorizontalDivider()
                Spacer(modifier = Modifier.height(4.dp))
                // Source Code row
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clickable {
                            val intent = Intent(Intent.ACTION_VIEW, Uri.parse("https://github.com/ravindu644/Droidspaces-OSS"))
                            context.startActivity(intent)
                        },
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
                Spacer(modifier = Modifier.height(4.dp))
                HorizontalDivider()
                Spacer(modifier = Modifier.height(4.dp))

                // Contributors
                Text(
                    text = context.getString(R.string.contributors),
                    style = MaterialTheme.typography.titleSmall,
                    fontWeight = FontWeight.SemiBold,
                    color = MaterialTheme.colorScheme.primary
                )
                // Load contributors from XML and display them
                val contributors = remember { ContributorManager.loadContributors(context) }
                contributors.forEach { contributor ->
                    ContributorItem(
                        contributor = contributor,
                        onClick = {
                            val intent = Intent(Intent.ACTION_VIEW, Uri.parse(contributor.githubUrl))
                            context.startActivity(intent)
                        }
                    )
                }
                }

                // OK Button
                Spacer(modifier = Modifier.height(8.dp))
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
private fun ContributorItem(
    contributor: Contributor,
    onClick: () -> Unit
) {
    val context = LocalContext.current

    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        Icon(
            imageVector = Icons.Default.Person,
            contentDescription = null,
            modifier = Modifier.size(20.dp),
            tint = MaterialTheme.colorScheme.onSurfaceVariant
        )
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = contributor.name,
                style = MaterialTheme.typography.bodyMedium,
                fontWeight = FontWeight.Medium
            )
            Text(
                text = contributor.role,
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

    ListItem(
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

    // Get available languages
    val availableLanguages = remember {
        LocaleHelper.getAvailableLanguages(context)
    }

    // Add "System Default" option
    val allOptions = remember(availableLanguages) {
        buildList {
            add("system" to context.getString(R.string.system_default))
            availableLanguages.forEach { language ->
                add(language.code to language.nativeName)
            }
        }
    }

    // Get current language code
    val currentLanguage = remember {
        LocaleHelper.getCurrentLanguageCode()
    }

    var selectedIndex by remember {
        mutableIntStateOf(allOptions.indexOfFirst { (code, _) -> code == currentLanguage }.takeIf { it >= 0 } ?: 0)
    }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = {
            Text(
                text = context.getString(R.string.language),
                style = MaterialTheme.typography.titleLarge,
                fontWeight = FontWeight.Bold
            )
        },
        text = {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .heightIn(max = 400.dp)
                    .verticalScroll(rememberScrollState())
            ) {
                allOptions.forEachIndexed { index, (_, displayName) ->
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clickable {
                                selectedIndex = index
                            }
                            .padding(horizontal = 8.dp, vertical = 4.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.Start
                    ) {
                        RadioButton(
                            selected = selectedIndex == index,
                            onClick = { selectedIndex = index },
                            modifier = Modifier.padding(end = 8.dp)
                        )
                        Text(
                            text = displayName,
                            style = MaterialTheme.typography.bodyLarge,
                            modifier = Modifier.weight(1f)
                        )
                    }
                }
            }
        },
        confirmButton = {
            TextButton(
                onClick = {
                    if (selectedIndex >= 0 && selectedIndex < allOptions.size) {
                        val languageCode = allOptions[selectedIndex].first
                        onLanguageSelected(languageCode)
                    }
                    onDismiss()
                }
            ) {
                Text(context.getString(R.string.ok))
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text(context.getString(R.string.cancel))
            }
        }
    )
}


