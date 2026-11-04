package com.droidspaces.app.ui.screen

import android.content.Context
import android.content.ClipData
import android.content.ClipboardManager
import android.content.pm.PackageManager
import androidx.compose.foundation.clickable
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.BorderStroke
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.OpenInNew
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.material3.RadioButton
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.painterResource
import android.content.Intent
import android.net.Uri
import com.droidspaces.app.ui.component.DsDialog
import com.droidspaces.app.R
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.droidspaces.app.ui.component.DialogCloseButton
import com.droidspaces.app.ui.component.DialogFooterRow
import com.droidspaces.app.ui.component.SectionHeader
import com.droidspaces.app.ui.component.AccentColorPicker
import com.droidspaces.app.ui.component.BugReportDialog
import com.droidspaces.app.ui.component.GroupDivider
import com.droidspaces.app.ui.component.GroupHeader
import com.droidspaces.app.ui.component.NavRow
import com.droidspaces.app.ui.component.SettingsGroup
import com.droidspaces.app.ui.component.SwitchItem
import com.droidspaces.app.ui.theme.ThemePalette
import com.droidspaces.app.ui.theme.rememberThemeState
import com.droidspaces.app.ui.viewmodel.AppStateViewModel
import com.droidspaces.app.util.PreferencesManager
import com.droidspaces.app.util.LocaleHelper
import com.droidspaces.app.util.ContributorManager
import android.graphics.BitmapFactory
import android.util.Base64
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.foundation.Image
import com.droidspaces.app.util.SymlinkInstaller
import com.droidspaces.app.util.DeviceArch
import com.droidspaces.app.util.HostCapabilities
import androidx.core.content.edit
import java.util.Locale
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import androidx.compose.runtime.rememberCoroutineScope

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(
    appStateViewModel: AppStateViewModel,
    onBack: () -> Unit,
    onNavigateToInstallation: () -> Unit = {},
    onNavigateToRequirements: () -> Unit = {},
    onNavigateToAutoBootPriority: () -> Unit = {},
    onNavigateToTerminalAppearance: () -> Unit = {}
) {
    val context = LocalContext.current
    val scrollBehavior = TopAppBarDefaults.pinnedScrollBehavior()
    val prefsManager = remember { PreferencesManager.getInstance(context) }
    val isRootAvailable = appStateViewModel.isRootAvailable
    // Boot order is pointless when nothing can boot: no root, or a kernel that
    // failed a MUST HAVE probe. Same dimming as the reinstall row above.
    val caps by HostCapabilities.state.collectAsState()
    val autoBootUsable = isRootAvailable && caps?.requirementsMet != false
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
    var chosenLanguage by remember { mutableStateOf<String?>(null) }
    var currentAppLocale by remember { mutableStateOf(LocaleHelper.getCurrentAppLocale(context)) }

    // Daemon mode + symlink state, collected reactively from prefs
    // (replaces a hand-rolled SharedPreferences change listener).
    val isDaemonModeEnabled by prefsManager.daemonModeFlow
        .collectAsStateWithLifecycle(initialValue = prefsManager.isDaemonModeEnabled)
    val isSymlinkEnabled by prefsManager.symlinkEnabledFlow
        .collectAsStateWithLifecycle(initialValue = prefsManager.isSymlinkEnabled)
    val treatAs64Bit by prefsManager.treatAs64BitFlow
        .collectAsStateWithLifecycle(initialValue = prefsManager.treatAs64Bit)

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
                        fontWeight = FontWeight.Bold
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
            GroupHeader(context.getString(R.string.backend_section))

            SettingsGroup {
                NavRow(
                    icon = Icons.Default.Build,
                    title = context.getString(R.string.reinstall_backend),
                    summary = context.getString(R.string.reinstall_backend_description),
                    enabled = isRootAvailable,
                    onClick = onNavigateToInstallation
                )

                GroupDivider()

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

                GroupDivider()

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

                GroupDivider()

                // Auto Boot Priority - clickable to navigate to the boot-order screen
                NavRow(
                    icon = Icons.Default.LowPriority,
                    title = context.getString(R.string.auto_boot_priority),
                    summary = context.getString(R.string.auto_boot_priority_description),
                    enabled = autoBootUsable,
                    onClick = onNavigateToAutoBootPriority
                )

                GroupDivider()

                NavRow(
                    icon = Icons.Default.Code,
                    title = context.getString(R.string.requirements),
                    summary = context.getString(R.string.requirements_for_droidspaces),
                    onClick = onNavigateToRequirements
                )
            }

            // Theme Section
            GroupHeader(context.getString(R.string.appearance_section))

            SettingsGroup {
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

                NavRow(
                    icon = Icons.Default.Translate,
                    title = context.getString(R.string.language),
                    value = currentLanguageDisplay,
                    onClick = { showLanguageDialog = true },
                    chevron = false
                )

                GroupDivider()

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
                    GroupDivider()
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
                    GroupDivider()
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
                    GroupDivider()
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
                    GroupDivider()
                    AccentColorPicker(
                        selectedPalette = themeState.themePalette,
                        isDarkTheme = darkTheme,
                        onPaletteSelected = { palette ->
                            prefsManager.themePalette = palette.name
                        }
                    )
                }

                // Terminal customization lives on its own page (dark mode, font size, ...)
                GroupDivider()
                NavRow(
                    icon = Icons.Default.Terminal,
                    title = context.getString(R.string.terminal),
                    summary = context.getString(R.string.terminal_appearance_summary),
                    onClick = onNavigateToTerminalAppearance
                )
            }

            // Updates Section
            GroupHeader(context.getString(R.string.updates_section))

            SettingsGroup {
                var checkAppUpdates by remember { mutableStateOf(prefsManager.checkAppUpdates) }
                SwitchItem(
                    icon = Icons.Default.SystemUpdate,
                    title = context.getString(R.string.check_app_updates),
                    summary = context.getString(R.string.check_app_updates_description),
                    checked = checkAppUpdates,
                    onCheckedChange = { checked ->
                        checkAppUpdates = checked
                        prefsManager.checkAppUpdates = checked
                        // Hides the Home banner right away, or fetches right away.
                        appStateViewModel.checkAppUpdate()
                    }
                )
            }

            // Debugging Section
            GroupHeader(context.getString(R.string.debugging_section))

            SettingsGroup {
                NavRow(
                    icon = Icons.Default.BugReport,
                    title = context.getString(R.string.generate_bug_report),
                    summary = context.getString(R.string.generate_bug_report_description),
                    enabled = isRootAvailable,
                    onClick = { showBugReportDialog = true },
                    chevron = false
                )
            }

            // Experimental Section, only for a 32-bit userspace on a 64-bit kernel
            if (DeviceArch.is32Bit()) {
                GroupHeader(context.getString(R.string.experimental_section))

                SettingsGroup {
                    SwitchItem(
                        icon = Icons.Default.Memory,
                        title = context.getString(R.string.treat_as_64bit),
                        summary = context.getString(R.string.treat_as_64bit_description),
                        checked = treatAs64Bit,
                        enabled = isRootAvailable,
                        onCheckedChange = { checked ->
                            prefsManager.treatAs64Bit = checked
                            // The hash check now compares against the other arch's
                            // asset, so the update banner appears without a restart.
                            appStateViewModel.checkBackendStatus(force = true)
                        }
                    )
                }
            }

            // About Section
            GroupHeader(context.getString(R.string.about_section))

            SettingsGroup {
                NavRow(
                    icon = Icons.Default.Info,
                    title = context.getString(R.string.about_droidspaces),
                    summary = getAppVersion(context),
                    onClick = { showAboutDialog = true },
                    chevron = false
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
            onLanguageSelected = { chosenLanguage = it }
        )
    }
    // Not from the OK button: below Android 13 the change recreates the activity at once. On Android
    // 12 a dialog removed during that relaunch stays on screen for 2 s and keeps the window manager's
    // focus, so the next Back is an ANR. An effect runs after the frame that removed the dialog.
    LaunchedEffect(chosenLanguage) {
        chosenLanguage?.let { LocaleHelper.changeLanguage(it) }
    }
}

@Composable
private fun AboutDialog(onDismiss: () -> Unit) {
    val context = LocalContext.current

    // Deviates from the full-width dismiss rule: About is an info page, not a
    // decision, so it closes from the header like the terminal log viewer,
    // pinned above the scrolling body. scrollableContent = false because the
    // header must not scroll away with it.
    DsDialog(
        onDismiss = onDismiss,
        modifier = Modifier.fillMaxHeight(0.85f),
        scrollableContent = false
    ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = context.getString(R.string.app_name),
                    style = MaterialTheme.typography.headlineSmall,
                    fontWeight = FontWeight.Bold
                )
                DialogCloseButton(onClick = onDismiss)
            }

            Column(
                modifier = Modifier
                    .weight(1f, fill = false)
                    .verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(12.dp)
            ) {
            Text(
                text = context.getString(R.string.about_description),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            SectionHeader(text = context.getString(R.string.developers))
            Surface(
                modifier = Modifier.fillMaxWidth(),
                shape = RoundedCornerShape(24.dp),
                color = MaterialTheme.colorScheme.surfaceContainerHigh,
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f))
            ) {
                Column {
                    LinkRow("https://github.com/ravindu644", context.getString(R.string.developer_ravindu644), context.getString(R.string.developer_ravindu644_role)) {
                        Icon(Icons.Default.Person, contentDescription = null, modifier = Modifier.size(24.dp), tint = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                    GroupDivider()
                    LinkRow("https://t.me/Droidspaces", context.getString(R.string.telegram_channel)) {
                        Icon(painterResource(R.drawable.ic_telegram), contentDescription = null, modifier = Modifier.size(24.dp), tint = MaterialTheme.colorScheme.primary)
                    }
                    GroupDivider()
                    LinkRow("https://github.com/ravindu644/Droidspaces-OSS", context.getString(R.string.source_code)) {
                        Icon(Icons.Default.Code, contentDescription = null, modifier = Modifier.size(24.dp), tint = MaterialTheme.colorScheme.primary)
                    }
                }
            }

            SectionHeader(text = context.getString(R.string.contributors))
            val contributors = remember { ContributorManager.load(context) }
            Surface(
                modifier = Modifier.fillMaxWidth(),
                shape = RoundedCornerShape(24.dp),
                color = MaterialTheme.colorScheme.surfaceContainerHigh,
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f))
            ) {
                Column {
                    contributors.forEachIndexed { index, contributor ->
                        if (index > 0) GroupDivider()
                        val avatar = remember(contributor.avatarB64) {
                            if (contributor.avatarB64.isNotEmpty()) runCatching {
                                val bytes = Base64.decode(contributor.avatarB64, Base64.DEFAULT)
                                BitmapFactory.decodeByteArray(bytes, 0, bytes.size)?.asImageBitmap()
                            }.getOrNull() else null
                        }
                        LinkRow(
                            contributor.githubUrl,
                            contributor.login,
                            context.resources.getQuantityString(R.plurals.commit_count, contributor.commits, contributor.commits)
                        ) {
                            if (avatar != null) Image(avatar, contentDescription = null, modifier = Modifier.size(28.dp).clip(CircleShape))
                            else Icon(Icons.Default.Person, contentDescription = null, modifier = Modifier.size(28.dp), tint = MaterialTheme.colorScheme.onSurfaceVariant)
                        }
                    }
                }
            }
        }
    }
}

/** A row that opens [url]: a leading icon or avatar, a title, an optional second line, the open-in-new mark. */
@Composable
private fun LinkRow(url: String, title: String, subtitle: String? = null, leading: @Composable () -> Unit) {
    val context = LocalContext.current
    Row(
        modifier = Modifier
            .fillMaxWidth()
            // No browser at all on a locked-down profile: the tap does nothing rather than crash.
            .clickable { runCatching { context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(url))) } }
            .padding(horizontal = 16.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        leading()
        Column(modifier = Modifier.weight(1f)) {
            Text(title, style = MaterialTheme.typography.bodyMedium, fontWeight = FontWeight.Medium)
            if (subtitle != null) Text(subtitle, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        Icon(
            Icons.AutoMirrored.Filled.OpenInNew,
            contentDescription = null,
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

    DsDialog(
        onDismiss = onDismiss,
        modifier = Modifier.heightIn(max = 560.dp),
        footer = {
            DialogFooterRow(
                dismissLabel = context.getString(R.string.cancel),
                confirmLabel = context.getString(R.string.ok),
                onDismiss = onDismiss,
                onConfirm = {
                    if (selectedIndex >= 0 && selectedIndex < allOptions.size) {
                        onLanguageSelected(allOptions[selectedIndex].first)
                    }
                    onDismiss()
                }
            )
        }
    ) {
        Text(
            text = context.getString(R.string.language),
            style = MaterialTheme.typography.titleLarge,
            fontWeight = FontWeight.Bold
        )

        Column(
            modifier = Modifier.fillMaxWidth(),
            verticalArrangement = Arrangement.spacedBy(4.dp)
        ) {
            allOptions.forEachIndexed { index, (_, displayName) ->
                val isSelected = selectedIndex == index
                Surface(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(16.dp))
                        .clickable { selectedIndex = index },
                    color = if (isSelected) MaterialTheme.colorScheme.primary.copy(alpha = 0.08f) else Color.Transparent,
                    shape = RoundedCornerShape(16.dp),
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
    
    }
    }



