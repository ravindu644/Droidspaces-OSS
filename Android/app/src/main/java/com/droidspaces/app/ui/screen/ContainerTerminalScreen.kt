package com.droidspaces.app.ui.screen

import com.droidspaces.app.ui.component.DsTopAppBar
import android.app.Activity
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.os.IBinder
import androidx.activity.compose.BackHandler
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Add
import kotlinx.coroutines.launch
import com.droidspaces.app.util.ContainerUsersManager
import com.droidspaces.app.ui.component.SegmentedGroup
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Person
import androidx.compose.material.icons.filled.ChevronRight
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Keyboard
import androidx.compose.material.icons.filled.KeyboardHide
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import com.droidspaces.app.ui.component.DsDialog
import com.droidspaces.app.service.TerminalSessionService
import com.droidspaces.app.ui.component.DialogFooterRow
import com.droidspaces.app.ui.terminal.TerminalBackEnd
import com.droidspaces.app.ui.terminal.TerminalScreenState
import com.droidspaces.app.ui.terminal.virtualkeys.VirtualKeysConstants
import com.droidspaces.app.ui.terminal.virtualkeys.VirtualKeysInfo
import com.droidspaces.app.ui.terminal.virtualkeys.VirtualKeysListener
import com.droidspaces.app.ui.terminal.virtualkeys.VirtualKeysView
import android.graphics.Typeface
import com.droidspaces.app.util.ContainerOSInfoManager
import com.droidspaces.app.util.FontInfo
import com.droidspaces.app.util.PreferencesManager
import java.io.File
import com.droidspaces.app.ui.util.LoadingIndicator
import com.droidspaces.app.ui.util.LoadingSize
import com.termux.terminal.TerminalSession
import com.termux.view.TerminalView
import java.lang.ref.WeakReference
import java.util.UUID
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.core.content.res.ResourcesCompat
import com.droidspaces.app.R

private data class TerminalTab(
    val id: String,
    val user: String,
    val label: String,
)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ContainerTerminalScreen(
    containerName: String,
    initialUsers: List<String>,
    onNavigateBack: () -> Unit,
) {
    val context = LocalContext.current
    val activity = context as? Activity
    val terminalDarkTheme = remember { PreferencesManager.getInstance(context).terminalDarkTheme }
    // Read once at entry like the theme above; re-enter to apply
    val confirmTabClose = remember { PreferencesManager.getInstance(context).terminalConfirmClose }

    val keyboardController = LocalSoftwareKeyboardController.current
    var binder by remember { mutableStateOf<TerminalSessionService.SessionBinder?>(null) }

    DisposableEffect(Unit) {
        val conn = object : ServiceConnection {
            override fun onServiceConnected(name: ComponentName?, service: IBinder?) {
                binder = service as? TerminalSessionService.SessionBinder
            }
            override fun onServiceDisconnected(name: ComponentName?) { binder = null }
        }
        context.bindService(Intent(context, TerminalSessionService::class.java), conn, Context.BIND_AUTO_CREATE)
        onDispose {
            // Detach the UI client from any backgrounded sessions before unbinding so
            // the service doesn't retain this Activity/TerminalView. The
            // screen re-attaches its own client on re-entry.
            binder?.detachAllClients()
            context.unbindService(conn)
        }
    }

    val availableUsers = remember(initialUsers) {
        val list = initialUsers.toMutableList()
        if (!list.contains("root")) list.add(0, "root")
        list
    }

    val tabs = remember { mutableStateListOf<TerminalTab>() }
    var activeTabId by remember { mutableStateOf("") }
    var showUserPicker by remember { mutableStateOf(false) }
    // Tab whose X was tapped while Confirm Before Closing is on
    var tabPendingClose by remember { mutableStateOf<TerminalTab?>(null) }

    // Resolve hostname reactively; picker is shown only after binder+hostname are both ready
    var hostname by remember(containerName) {
        mutableStateOf(
            ContainerOSInfoManager.getCachedOSInfo(containerName, context)?.hostname
                ?: containerName.take(12)
        )
    }
    var hostnameReady by remember { mutableStateOf(hostname != containerName.take(12)) }
    LaunchedEffect(containerName) {
        val resolved = ContainerOSInfoManager.getCachedOSInfo(containerName, context)?.hostname
            ?: ContainerOSInfoManager.fetchAll(context)[containerName]?.hostname
        if (resolved != null) hostname = resolved
        hostnameReady = true
    }

    LaunchedEffect(binder, hostnameReady) {
        binder ?: return@LaunchedEffect
        if (!hostnameReady) return@LaunchedEffect
        if (tabs.isNotEmpty()) return@LaunchedEffect
        val existing = TerminalSessionService.globalSessionList
            .filter { (_, info) -> info.containerName == containerName }
        if (existing.isNotEmpty()) {
            existing.forEach { (id, info) ->
                tabs.add(TerminalTab(id = id, user = info.user, label = "${info.user}@$hostname"))
            }
            activeTabId = existing.keys.last()
        } else {
            showUserPicker = true
        }
    }

    // Sync UI tabs with background service reality.
    // If sessions are killed externally (e.g. Notification Exit), remove them here.
    LaunchedEffect(TerminalSessionService.globalSessionList.size) {
        val currentGlobalIds = TerminalSessionService.globalSessionList.keys
        val toRemove = tabs.filter { it.id !in currentGlobalIds }
        if (toRemove.isNotEmpty()) {
            val wasActiveRemoved = activeTabId in toRemove.map { it.id }
            tabs.removeAll(toRemove)
            if (tabs.isEmpty()) {
                onNavigateBack()
            } else if (wasActiveRemoved) {
                activeTabId = tabs.last().id
            }
        }
    }

    fun addTab(user: String) {
        TerminalSessionService.start(context)
        val id = "${containerName}_${UUID.randomUUID().toString().take(8)}"
        val newTab = TerminalTab(id = id, user = user, label = "$user@$hostname")
        val currentIndex = tabs.indexOfFirst { it.id == activeTabId }
        if (currentIndex != -1) {
            tabs.add(currentIndex + 1, newTab)
        } else {
            tabs.add(newTab)
        }
        activeTabId = id
    }

    fun closeTab(tab: TerminalTab) {
        // terminateSession handles the immediate UI state update (globalSessionList.remove)
        // and internal cleanup delays (EOF propagation).
        binder?.terminateSession(tab.id)

        if (tabs.size == 1) keyboardController?.hide()
        val idx = tabs.indexOf(tab)
        tabs.remove(tab)
        if (tabs.isEmpty()) onNavigateBack()
        else activeTabId = tabs.getOrElse(idx.coerceAtMost(tabs.lastIndex)) { tabs.last() }.id
    }

    val exitScreen = {
        keyboardController?.hide()
        onNavigateBack()
    }

    // Physical back leaves sessions alive in the service.
    BackHandler { exitScreen() }

    tabPendingClose?.let { tab ->
        DsDialog(
            onDismiss = { tabPendingClose = null },
            footer = {
                DialogFooterRow(
                    dismissLabel = context.getString(R.string.cancel),
                    confirmLabel = context.getString(R.string.ok),
                    onDismiss = { tabPendingClose = null },
                    onConfirm = {
                        closeTab(tab)
                        tabPendingClose = null
                    },
                )
            }
        ) {
            Text(
                text = context.getString(R.string.terminal_close_tab_title),
                style = MaterialTheme.typography.titleLarge,
                fontWeight = FontWeight.Bold
            )
            Text(
                text = context.getString(R.string.terminal_close_tab_message),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }

    if (showUserPicker) {
        NewSessionSheet(
            containerName = containerName,
            initialUsers = availableUsers,
            onConfirm = { user ->
                showUserPicker = false
                addTab(user)
            },
            onDismiss = {
                showUserPicker = false
                if (tabs.isEmpty()) exitScreen()
            }
        )
    }

    Scaffold(
        topBar = {
            Column {
                // No title: each session tab below already reads user@hostname, and the
                // row it would take goes to the terminal instead
                DsTopAppBar(
                    title = "",
                    onBack = { exitScreen() },
                    actions = {
                        IconButton(onClick = { showUserPicker = true }) {
                            Icon(Icons.Default.Add, contentDescription = "New tab")
                        }
                    }
                )

                if (tabs.isNotEmpty()) {
                    PrimaryScrollableTabRow(
                        selectedTabIndex = tabs.indexOfFirst { it.id == activeTabId }.coerceAtLeast(0),
                        // Matches the app bar above it so the two read as one header band
                        containerColor = MaterialTheme.colorScheme.surface,
                        contentColor = MaterialTheme.colorScheme.primary,
                        edgePadding = 0.dp,
                        divider = {},
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        tabs.forEach { tab ->
                            val isSelected = tab.id == activeTabId
                            Tab(
                                selected = isSelected,
                                onClick = { activeTabId = tab.id },
                                modifier = Modifier.height(40.dp)
                            ) {
                                Row(
                                    verticalAlignment = Alignment.CenterVertically,
                                    horizontalArrangement = Arrangement.spacedBy(4.dp),
                                    modifier = Modifier.padding(horizontal = 8.dp)
                                ) {
                                    Text(
                                        tab.label,
                                        style = MaterialTheme.typography.labelMedium,
                                        fontWeight = if (isSelected) FontWeight.SemiBold else FontWeight.Normal,
                                        maxLines = 1,
                                        overflow = TextOverflow.Ellipsis,
                                        modifier = Modifier.widthIn(max = 120.dp)
                                    )
                                    Box(
                                        Modifier.size(16.dp).clip(CircleShape),
                                        contentAlignment = Alignment.Center
                                    ) {
                                        IconButton(
                                            // Only the X asks for confirmation; a shell that already
                                            // exited closes through onSessionFinished unasked
                                            onClick = { if (confirmTabClose) tabPendingClose = tab else closeTab(tab) },
                                            modifier = Modifier.size(16.dp)
                                        ) {
                                            Icon(
                                                Icons.Default.Close,
                                                contentDescription = "Close tab",
                                                modifier = Modifier.size(12.dp),
                                                tint = if (isSelected) MaterialTheme.colorScheme.primary
                                                       else MaterialTheme.colorScheme.onSurfaceVariant
                                            )
                                        }
                                    }
                                }
                            }
                        }
                    }
                    HorizontalDivider(thickness = 0.5.dp, color = MaterialTheme.colorScheme.outlineVariant)
                }
            }
        }
    ) { paddingValues ->
        Box(
            modifier = Modifier
                .fillMaxSize()
                .padding(top = paddingValues.calculateTopPadding())
                .background(if (terminalDarkTheme) Color.Black else MaterialTheme.colorScheme.surface)
        ) {
            if (binder == null || tabs.isEmpty()) {
                Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                    LoadingIndicator(size = LoadingSize.Medium)
                }
            } else {
                tabs.forEach { tab ->
                    key(tab.id) {
                        TerminalTabView(
                            tab = tab,
                            binder = binder!!,
                            containerName = containerName,
                            isVisible = tab.id == activeTabId,
                            activity = activity,
                            onSessionFinished = { closeTab(tab) },
                            modifier = Modifier.fillMaxSize()
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun TerminalTabView(
    tab: TerminalTab,
    binder: TerminalSessionService.SessionBinder,
    containerName: String,
    isVisible: Boolean,
    activity: Activity?,
    onSessionFinished: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val context = androidx.compose.ui.platform.LocalContext.current
    val prefsManager = remember { PreferencesManager.getInstance(context) }
    val fontSizePx = TerminalSessionService.globalSessionList[tab.id]?.fontSizePx ?: prefsManager.terminalFontSizePx
    // Read once at entry, re-enter to apply, same contract as terminalDarkTheme below.
    // A missing or corrupt custom font falls back to bundled JetBrains Mono, and a
    // null bundled font falls back to the system default.
    val terminalTypeface = remember {
        prefsManager.terminalFontFile
            .takeIf { it.isNotEmpty() }
            ?.let { File(FontInfo.fontsDir(context), it) }
            ?.takeIf { it.isFile }
            ?.let { runCatching { Typeface.createFromFile(it) }.getOrNull() }
            ?: ResourcesCompat.getFont(context, R.font.jetbrains_mono)
    }

    // Terminal-only dark mode: renders the terminal page dark even when the rest
    // of the app follows the light theme. Read once at entry; re-enter to apply.
    val terminalDarkTheme = remember { prefsManager.terminalDarkTheme }
    // When taps no longer raise the keyboard, the button on the terminal is the
    // only way to get it back, so both are read together at entry.
    val tapOpensKeyboard = remember { prefsManager.terminalTapKeyboard }
    val keyboardButtonLeft = remember { prefsManager.terminalKeyboardLeft }
    // Holds this tab's client so the button acts on the terminal it sits on,
    // rather than on whichever view TerminalScreenState last saw.
    var backEnd by remember { mutableStateOf<TerminalBackEnd?>(null) }
    // Termux TerminalColors indices: 256 = default foreground, 257 = background,
    // 258 = cursor. Dark mode uses the classic termux white-on-black scheme:
    // pure white foreground on pure black background.
    val terminalForeground = if (terminalDarkTheme) Color.White.toArgb() else MaterialTheme.colorScheme.onSurface.toArgb()
    // Only dark mode explicitly overrides the full-screen background (View paint)
    // and the default background color (index 257). In light mode we keep the
    // pre-PR behavior: the Activity background shows through and the Termux
    // default background color is left untouched.
    val terminalBackground = if (terminalDarkTheme) Color.Black.toArgb() else 0
    // Stays a literal: the keys sit against the terminal's own black, so they have to
    // read dark even when the app itself is in the light theme.
    val virtualKeysBackground = if (terminalDarkTheme) Color(0xFF1A1A1E) else MaterialTheme.colorScheme.surfaceContainerHighest
    // The keys themselves: tonal pills, and a latched CTRL or ALT filled in primary like a
    // checked toggle button. On the dark terminal a faint white lift stands in for the
    // tonal fill, which would follow the app theme instead of the terminal's black.
    val keyFill = (if (terminalDarkTheme) Color.White.copy(alpha = 0.08f) else MaterialTheme.colorScheme.secondaryContainer).toArgb()
    val keyActiveFill = MaterialTheme.colorScheme.primary.toArgb()
    val keyActiveText = MaterialTheme.colorScheme.onPrimary.toArgb()

    AnimatedVisibility(
        visible = isVisible,
        enter = fadeIn(animationSpec = MaterialTheme.motionScheme.fastEffectsSpec()),
        exit = fadeOut(animationSpec = MaterialTheme.motionScheme.fastEffectsSpec()),
        modifier = modifier
    ) {
        Column(modifier = Modifier.fillMaxSize()) {

            // The button rides inside the terminal's own Box, which is what keeps
            // it clear of the virtual keys row without hardcoding its height.
            Box(modifier = Modifier.fillMaxWidth().weight(1f)) {
                AndroidView(
                    factory = { ctx ->
                        TerminalView(ctx, null).apply {
                            TerminalScreenState.terminalView = WeakReference(this)
                            setTextSize(fontSizePx)      // must run first, initializes mRenderer
                            setTypeface(terminalTypeface) // JetBrains Mono; null = system default
                            keepScreenOn = true
                            isFocusableInTouchMode = true
                            // The renderer only paints cell backgrounds; the full-screen
                            // default background comes from the View itself.
                            if (terminalDarkTheme) {
                                setBackgroundColor(terminalBackground)
                            }

                            if (activity != null) {
                                val client = TerminalBackEnd(
                                    terminal = this,
                                    activity = activity,
                                    initialFontSizePx = fontSizePx,
                                    onSessionFinished = onSessionFinished,
                                    onFontSizeChanged = { newSize ->
                                        TerminalSessionService.globalSessionList[tab.id]?.let { info ->
                                            TerminalSessionService.globalSessionList[tab.id] = info.copy(fontSizePx = newSize)
                                        }
                                    },
                                )
                                val session: TerminalSession =
                                    binder.getSession(tab.id) ?: binder.createSession(
                                        containerName = containerName,
                                        client = client,
                                        containerUser = tab.user,
                                        sessionId = tab.id,
                                    )
                                session.updateTerminalSessionClient(client)
                                attachSession(session)
                                setTerminalViewClient(client)
                                backEnd = client
                            }

                            post {
                                requestFocus()
                                (mClient as? TerminalBackEnd)?.activate12KeyInputMethodIfNeeded()
                                mEmulator?.mColors?.mCurrentColors?.apply {
                                    set(256, terminalForeground)
                                    set(258, terminalForeground)
                                    if (terminalDarkTheme) {
                                        set(257, terminalBackground)
                                    }
                                }
                            }
                        }
                    },
                    update = { tv ->
                        if (isVisible) {
                            // Re-apply before setTextSize: termux renderer resets typeface on size changes
                            tv.setTypeface(terminalTypeface)
                            tv.setTextSize(fontSizePx)
                            tv.onScreenUpdated()
                            TerminalScreenState.terminalView = WeakReference(tv)
                        }
                    },
                    modifier = Modifier.fillMaxSize()
                )

                if (!tapOpensKeyboard) {
                    KeyboardToggleButton(
                        darkTheme = terminalDarkTheme,
                        onToggle = { imeVisible ->
                            if (imeVisible) backEnd?.hideSoftInput() else backEnd?.showSoftInput()
                        },
                        modifier = Modifier
                            .align(if (keyboardButtonLeft) Alignment.BottomStart else Alignment.BottomEnd)
                            .padding(16.dp)
                    )
                }
            }

            AndroidView(
                factory = { ctx ->
                    VirtualKeysView(ctx, null).apply {
                        TerminalScreenState.virtualKeysView = WeakReference(this)
                        binder.getSession(tab.id)?.let { virtualKeysViewClient = VirtualKeysListener(it) }
                        setButtonColors(terminalForeground, keyActiveText, keyFill, keyActiveFill)
                        try {
                            reload(VirtualKeysInfo(VIRTUAL_KEYS_LAYOUT, "", VirtualKeysConstants.CONTROL_CHARS_ALIASES))
                        } catch (e: Exception) {
                            e.printStackTrace()
                        }
                    }
                },
                update = { vkv ->
                    if (isVisible) {
                        TerminalScreenState.virtualKeysView = WeakReference(vkv)
                        vkv.buttonTextColor = terminalForeground
                        binder.getSession(tab.id)?.let { vkv.virtualKeysViewClient = VirtualKeysListener(it) }
                    }
                },
                modifier = Modifier
                    .fillMaxWidth()
                    .background(virtualKeysBackground)
                    .windowInsetsPadding(WindowInsets.safeDrawing.only(WindowInsetsSides.Bottom))
                    .padding(horizontal = 6.dp, vertical = 4.dp)
                    .height(96.dp)
            )
        }
    }
}

/**
 * Manual keyboard control for when taps are handed to the terminal instead. It
 * toggles, so the same button also puts the keyboard away.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun KeyboardToggleButton(
    darkTheme: Boolean,
    onToggle: (Boolean) -> Unit,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current
    val imeVisible = WindowInsets.isImeVisible
    // Literals for the same reason virtualKeysBackground above uses them, and the
    // same decided exception in DESIGN.md: this sits on the terminal's own black,
    // so it has to read dark even when the app itself is in the light theme.
    val fill = if (darkTheme) Color(0xFF1A1A1E) else MaterialTheme.colorScheme.surfaceContainerHighest
    val accent = if (darkTheme) Color.White else MaterialTheme.colorScheme.onSurface
    val edge = if (darkTheme) Color.White.copy(alpha = 0.15f)
               else MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f)

    Surface(
        onClick = { onToggle(imeVisible) },
        modifier = modifier.size(56.dp),
        shape = RoundedCornerShape(16.dp),
        // Slightly transparent so it never fully hides a line of output under it
        color = fill.copy(alpha = 0.9f),
        border = BorderStroke(1.dp, edge),
        tonalElevation = 0.dp
    ) {
        Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
            Icon(
                imageVector = if (imeVisible) Icons.Default.KeyboardHide else Icons.Default.Keyboard,
                contentDescription = context.getString(R.string.terminal_toggle_keyboard),
                tint = accent,
                modifier = Modifier.size(28.dp)
            )
        }
    }
}

/**
 * The new session picker: one row per user, root first. A tap opens the shell, there is
 * nothing to confirm. Refresh re-reads the container's users, for one added from inside
 * it since the screen opened.
 */
@OptIn(ExperimentalMaterial3Api::class, ExperimentalMaterial3ExpressiveApi::class)
@Composable
private fun NewSessionSheet(
    containerName: String,
    initialUsers: List<String>,
    onConfirm: (String) -> Unit,
    onDismiss: () -> Unit,
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var users by remember { mutableStateOf(initialUsers) }

    ModalBottomSheet(
        onDismissRequest = onDismiss,
        // The sheet component's own 28; the theme's extraLarge is 24 for cards
        shape = RoundedCornerShape(topStart = 28.dp, topEnd = 28.dp),
        containerColor = MaterialTheme.colorScheme.surfaceContainerLow,
        sheetMaxWidth = 640.dp
    ) {
        Column(Modifier.padding(start = 16.dp, end = 16.dp, bottom = 24.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f).padding(start = 8.dp)) {
                    Text(context.getString(R.string.new_session), style = MaterialTheme.typography.headlineSmall)
                    Text(
                        context.getString(R.string.new_session_subtitle, containerName),
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                IconButton(onClick = {
                    scope.launch {
                        val fresh = ContainerUsersManager.getUsers(containerName, useCache = false)
                        users = listOf("root") + fresh.filter { it != "root" }
                    }
                }) {
                    Icon(Icons.Default.Refresh, context.getString(R.string.refresh_users))
                }
            }
            Spacer(Modifier.height(16.dp))
            SegmentedGroup(count = users.size) { index, shape ->
                val user = users[index]
                val root = user == "root"
                Surface(onClick = { onConfirm(user) }, shape = shape, color = MaterialTheme.colorScheme.surfaceContainer) {
                    ListItem(
                        headlineContent = { Text(user, style = MaterialTheme.typography.bodyLargeEmphasized) },
                        supportingContent = { Text(context.getString(if (root) R.string.user_superuser else R.string.user_regular)) },
                        leadingContent = {
                            Surface(
                                shape = CircleShape,
                                color = if (root) MaterialTheme.colorScheme.primaryContainer else MaterialTheme.colorScheme.secondaryContainer,
                                modifier = Modifier.size(40.dp)
                            ) {
                                Box(contentAlignment = Alignment.Center) {
                                    Icon(Icons.Default.Person, contentDescription = null, modifier = Modifier.size(24.dp))
                                }
                            }
                        },
                        trailingContent = { Icon(Icons.Default.ChevronRight, contentDescription = null) },
                        colors = ListItemDefaults.colors(containerColor = Color.Transparent)
                    )
                }
            }
        }
    }
}

private val VIRTUAL_KEYS_LAYOUT = """
[
  [
    "ESC",
    {"key": "/", "popup": "\\"},
    {"key": "-", "popup": "|"},
    "HOME",
    "UP",
    "END",
    "PGUP"
  ],
  [
    "TAB",
    "CTRL",
    "ALT",
    "LEFT",
    "DOWN",
    "RIGHT",
    "PGDN"
  ]
]
""".trimIndent()
