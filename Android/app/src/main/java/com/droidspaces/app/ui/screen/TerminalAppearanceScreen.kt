package com.droidspaces.app.ui.screen

import com.droidspaces.app.ui.component.DsTopAppBar
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.ChevronRight
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.automirrored.filled.KeyboardBackspace
import androidx.compose.material.icons.filled.FontDownload
import androidx.compose.material.icons.filled.Keyboard
import androidx.compose.material.icons.filled.FormatSize
import androidx.compose.material.icons.filled.Terminal
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.droidspaces.app.R
import com.droidspaces.app.ui.component.DsSlider
import com.droidspaces.app.ui.component.SectionLabel
import com.droidspaces.app.ui.component.SwitchItem
import com.droidspaces.app.ui.component.TerminalFontDialog
import com.droidspaces.app.ui.theme.JetBrainsMono
import com.droidspaces.app.ui.theme.rememberThemeState
import com.droidspaces.app.util.FontInfo
import com.droidspaces.app.util.PreferencesManager
import java.io.File
import kotlin.math.roundToInt

// Same bounds pinch-to-zoom enforces in TerminalBackEnd
private const val FONT_SIZE_MIN_PX = 6f
private const val FONT_SIZE_MAX_PX = 72f

/**
 * Terminal customization page, reached from the Appearance section of Settings.
 * Settings that only affect the terminal live here so the root settings page
 * stays flat while more terminal options accumulate.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun TerminalAppearanceScreen(onBack: () -> Unit) {
    val context = LocalContext.current
    val scrollBehavior = TopAppBarDefaults.pinnedScrollBehavior()
    val prefsManager = remember { PreferencesManager.getInstance(context) }
    val darkTheme = rememberThemeState().darkTheme

    // Local mirrors so the controls update immediately without waiting on a
    // SharedPreferences listener, same reasoning as the toggles on SettingsScreen.
    var terminalDarkTheme by remember { mutableStateOf(prefsManager.terminalDarkTheme) }
    var fontSizePx by remember { mutableFloatStateOf(prefsManager.terminalFontSizePx.toFloat()) }
    var confirmClose by remember { mutableStateOf(prefsManager.terminalConfirmClose) }
    var tapKeyboard by remember { mutableStateOf(prefsManager.terminalTapKeyboard) }
    var keyboardLeft by remember { mutableStateOf(prefsManager.terminalKeyboardLeft) }

    var selectedFont by remember { mutableStateOf(prefsManager.terminalFontFile) }
    var showFontDialog by remember { mutableStateOf(false) }
    // Re-resolved when the dialog closes too, since a delete inside it can
    // remove the file the selection points at
    // The card previews the selection in its own face, same as the picker rows
    val (fontSummary, fontFamily) = remember(selectedFont, showFontDialog) {
        val file = selectedFont.takeIf { it.isNotEmpty() }
            ?.let { File(FontInfo.fontsDir(context), it) }
            ?.takeIf { it.isFile }
        if (file == null) context.getString(R.string.terminal_font_default) to JetBrainsMono
        else FontInfo.displayName(file) to (FontInfo.family(file) ?: JetBrainsMono)
    }

    val cardColor = if (darkTheme) MaterialTheme.colorScheme.surfaceContainerHigh else MaterialTheme.colorScheme.surfaceContainer
    val cardBorder = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f))

    Scaffold(
        containerColor = Color.Transparent,
        modifier = Modifier.nestedScroll(scrollBehavior.nestedScrollConnection),
        topBar = {
            DsTopAppBar(title = context.getString(R.string.terminal), onBack = onBack, scrollBehavior = scrollBehavior)
        },
        contentWindowInsets = WindowInsets(0)
    ) { innerPadding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
                .verticalScroll(rememberScrollState())
        ) {
            Spacer(modifier = Modifier.height(12.dp))

            SectionLabel(
                text = context.getString(R.string.terminal_font),
                modifier = Modifier.padding(start = 24.dp, bottom = 8.dp)
            )

            Surface(
                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp),
                shape = RoundedCornerShape(24.dp),
                color = cardColor,
                border = cardBorder
            ) {
                Column {
                    // Font family lives in a dialog, like the env vars picker
                    ListItem(
                        colors = ListItemDefaults.colors(containerColor = Color.Transparent),
                        leadingContent = {
                            Icon(
                                imageVector = Icons.Default.FontDownload,
                                contentDescription = null
                            )
                        },
                        headlineContent = {
                            Text(
                                text = context.getString(R.string.terminal_font_family),
                                style = MaterialTheme.typography.titleMedium,
                                fontWeight = FontWeight.SemiBold
                            )
                        },
                        supportingContent = {
                            Text(fontSummary, fontFamily = fontFamily)
                        },
                        trailingContent = {
                            Icon(
                                imageVector = Icons.Default.ChevronRight,
                                contentDescription = null
                            )
                        },
                        modifier = Modifier.clickable { showFontDialog = true }
                    )

                    HorizontalDivider(modifier = Modifier.padding(horizontal = 16.dp), color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.3f))

                    // Plain layout rather than a ListItem: the slider needs the full row
                    // width and ListItem's fixed measure slots are the wrong home for it.
                    Column(modifier = Modifier.padding(horizontal = 16.dp, vertical = 12.dp)) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Icon(
                                imageVector = Icons.Default.FormatSize,
                                contentDescription = null
                            )
                            Spacer(modifier = Modifier.width(16.dp))
                            Column(modifier = Modifier.weight(1f)) {
                                Text(
                                    text = context.getString(R.string.terminal_font_size),
                                    style = MaterialTheme.typography.titleMedium,
                                    fontWeight = FontWeight.SemiBold
                                )
                                Text(
                                    text = context.getString(R.string.terminal_font_size_description),
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
                                )
                            }
                            Spacer(modifier = Modifier.width(16.dp))
                            Text(
                                text = "${fontSizePx.roundToInt()} px",
                                style = MaterialTheme.typography.titleMedium,
                                fontWeight = FontWeight.SemiBold,
                                color = MaterialTheme.colorScheme.primary
                            )
                        }

                        Spacer(modifier = Modifier.height(8.dp))

                        DsSlider(
                            value = fontSizePx,
                            onValueChange = { fontSizePx = it },
                            // Persist on release, not per drag tick
                            onValueChangeFinished = {
                                prefsManager.terminalFontSizePx = fontSizePx.roundToInt()
                            },
                            valueRange = FONT_SIZE_MIN_PX..FONT_SIZE_MAX_PX
                        )
                    }
                }
            }

            Spacer(modifier = Modifier.height(8.dp))

            SectionLabel(
                text = context.getString(R.string.terminal_section_theme),
                modifier = Modifier.padding(start = 24.dp, bottom = 8.dp, top = 8.dp)
            )

            Surface(
                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp),
                shape = RoundedCornerShape(24.dp),
                color = cardColor,
                border = cardBorder
            ) {
                // Terminal Dark Mode - independent of the app-wide theme
                SwitchItem(
                    icon = Icons.Default.Terminal,
                    title = context.getString(R.string.terminal_dark_theme),
                    summary = context.getString(R.string.terminal_dark_theme_description),
                    checked = terminalDarkTheme,
                    onCheckedChange = { checked ->
                        prefsManager.terminalDarkTheme = checked
                        terminalDarkTheme = checked
                    }
                )
            }

            Spacer(modifier = Modifier.height(8.dp))

            SectionLabel(
                text = context.getString(R.string.terminal_section_misc),
                modifier = Modifier.padding(start = 24.dp, bottom = 8.dp, top = 8.dp)
            )

            Surface(
                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp),
                shape = RoundedCornerShape(24.dp),
                color = cardColor,
                border = cardBorder
            ) {
                Column {
                    // Opt-in: guards the X on a terminal tab against fat fingers
                    SwitchItem(
                        icon = Icons.Default.Close,
                        title = context.getString(R.string.terminal_confirm_close),
                        summary = context.getString(R.string.terminal_confirm_close_description),
                        checked = confirmClose,
                        onCheckedChange = { checked ->
                            prefsManager.terminalConfirmClose = checked
                            confirmClose = checked
                        }
                    )

                    HorizontalDivider(modifier = Modifier.padding(horizontal = 16.dp), color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.3f))

                    SwitchItem(
                        icon = Icons.Default.Keyboard,
                        title = context.getString(R.string.terminal_tap_keyboard),
                        summary = context.getString(R.string.terminal_tap_keyboard_description),
                        checked = tapKeyboard,
                        onCheckedChange = { checked ->
                            prefsManager.terminalTapKeyboard = checked
                            tapKeyboard = checked
                        }
                    )

                    // Nothing to place while taps still open the keyboard
                    AnimatedVisibility(visible = !tapKeyboard) {
                        SwitchItem(
                            icon = Icons.AutoMirrored.Filled.KeyboardBackspace,
                            title = context.getString(R.string.terminal_keyboard_left),
                            summary = context.getString(R.string.terminal_keyboard_left_description),
                            checked = keyboardLeft,
                            onCheckedChange = { checked ->
                                prefsManager.terminalKeyboardLeft = checked
                                keyboardLeft = checked
                            }
                        )
                    }
                }
            }

            Spacer(modifier = Modifier.height(12.dp))
        }
    }

    if (showFontDialog) {
        TerminalFontDialog(
            initialSelection = selectedFont,
            onConfirm = { fileName ->
                prefsManager.terminalFontFile = fileName
                selectedFont = fileName
                showFontDialog = false
            },
            onDismiss = { showFontDialog = false }
        )
    }
}
