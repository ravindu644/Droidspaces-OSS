package com.droidspaces.app.ui.screen

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
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.tween
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.border
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Close
import androidx.compose.material3.*
import androidx.compose.material3.TabRowDefaults.tabIndicatorOffset
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import com.droidspaces.app.service.TerminalSessionService
import com.droidspaces.app.ui.terminal.TerminalBackEnd
import com.droidspaces.app.ui.terminal.TerminalScreenState
import com.droidspaces.app.ui.terminal.virtualkeys.VirtualKeysConstants
import com.droidspaces.app.ui.terminal.virtualkeys.VirtualKeysInfo
import com.droidspaces.app.ui.terminal.virtualkeys.VirtualKeysListener
import com.droidspaces.app.ui.terminal.virtualkeys.VirtualKeysView
import com.droidspaces.app.util.AnimationUtils
import com.droidspaces.app.util.ContainerOSInfoManager
import com.droidspaces.app.ui.util.LoadingIndicator
import com.droidspaces.app.ui.util.LoadingSize
import com.droidspaces.app.ui.component.DialogFooterRow
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

@OptIn(ExperimentalMaterial3Api::class, androidx.compose.foundation.ExperimentalFoundationApi::class)
@Composable
fun ContainerTerminalScreen(
    containerName: String,
    initialUsers: List<String>,
    onNavigateBack: () -> Unit,
) {
    val context = LocalContext.current
    val activity = context as? Activity

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
            // the service doesn't retain this Activity/TerminalView (VULN V16). The
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
    var tabToClose by remember { mutableStateOf<TerminalTab?>(null) }

    // Resolve hostname reactively; picker is shown only after binder+hostname are both ready
    var hostname by remember(containerName) {
        mutableStateOf(
            ContainerOSInfoManager.getCachedOSInfo(containerName, context)?.hostname
                ?: containerName.take(12)
        )
    }
    var hostnameReady by remember { mutableStateOf(hostname != containerName.take(12)) }
    LaunchedEffect(containerName) {
        val resolved = ContainerOSInfoManager.getOSInfo(containerName, useCache = true, appContext = context).hostname
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

    if (showUserPicker) {
        UserPickerDialog(
            users = availableUsers,
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

    if (tabToClose != null) {
        CloseSessionDialog(
            onConfirm = {
                val tab = tabToClose!!
                tabToClose = null
                closeTab(tab)
            },
            onDismiss = {
                tabToClose = null
            }
        )
    }

    Scaffold(
        topBar = {
            Column {
                CenterAlignedTopAppBar(
                    title = {
                        Text(
                            containerName,
                            style = MaterialTheme.typography.titleMedium,
                            fontWeight = FontWeight.SemiBold,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                        )
                    },
                    navigationIcon = {
                        IconButton(onClick = { exitScreen() }) {
                            Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
                        }
                    },
                    actions = {
                        IconButton(onClick = { showUserPicker = true }) {
                            Icon(Icons.Default.Add, contentDescription = "New tab")
                        }
                    },
                    colors = TopAppBarDefaults.centerAlignedTopAppBarColors(
                        containerColor = MaterialTheme.colorScheme.surfaceContainerLow
                    )
                )

                if (tabs.isNotEmpty()) {
                    val selectedTabIndex = tabs.indexOfFirst { it.id == activeTabId }.coerceAtLeast(0)
                    ScrollableTabRow(
                        selectedTabIndex = selectedTabIndex,
                        containerColor = MaterialTheme.colorScheme.surfaceContainerLow,
                        contentColor = MaterialTheme.colorScheme.primary,
                        edgePadding = 0.dp,
                        divider = {},
                        indicator = { tabPositions ->
                            if (selectedTabIndex < tabPositions.size) {
                                val position = tabPositions[selectedTabIndex]
                                Box(
                                    Modifier
                                        .tabIndicatorOffset(position)
                                        .fillMaxHeight()
                                        .padding(horizontal = 4.dp, vertical = 6.dp)
                                        .border(
                                            width = 1.dp,
                                            color = MaterialTheme.colorScheme.primary.copy(alpha = 0.2f),
                                            shape = RoundedCornerShape(16.dp)
                                        )
                                        .padding(4.dp)
                                        .border(
                                            width = 1.dp,
                                            color = MaterialTheme.colorScheme.primary.copy(alpha = 0.4f),
                                            shape = RoundedCornerShape(12.dp)
                                        )
                                        .background(
                                            color = MaterialTheme.colorScheme.primary.copy(alpha = 0.12f),
                                            shape = RoundedCornerShape(12.dp)
                                        )
                                )
                            }
                        },
                        modifier = Modifier.fillMaxWidth().height(48.dp)
                    ) {
                        tabs.forEach { tab ->
                            val isSelected = tab.id == activeTabId
                            Box(
                                modifier = Modifier
                                    .height(48.dp)
                                    .clip(RoundedCornerShape(16.dp))
                                    .combinedClickable(
                                        onClick = { activeTabId = tab.id },
                                        onLongClick = { tabToClose = tab }
                                    ),
                                contentAlignment = Alignment.Center
                            ) {
                                Text(
                                    text = tab.label,
                                    style = MaterialTheme.typography.labelMedium,
                                    fontWeight = if (isSelected) FontWeight.SemiBold else FontWeight.Normal,
                                    maxLines = 1,
                                    overflow = TextOverflow.Ellipsis,
                                    color = if (isSelected) MaterialTheme.colorScheme.primary
                                            else MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f),
                                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp)
                                )
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
                .background(MaterialTheme.colorScheme.surface)
        ) {
            if (binder == null || tabs.isEmpty()) {
                Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                    LoadingIndicator(size = LoadingSize.Medium)
                }
            } else {
                val selectedTabIndex = tabs.indexOfFirst { it.id == activeTabId }.coerceAtLeast(0)
                var previousActiveIndex by remember { mutableIntStateOf(0) }
                LaunchedEffect(selectedTabIndex) {
                    previousActiveIndex = selectedTabIndex
                }
                val isMovingRight = selectedTabIndex > previousActiveIndex
                val hasMoved = selectedTabIndex != previousActiveIndex

                tabs.forEachIndexed { index, tab ->
                    key(tab.id) {
                        TerminalTabView(
                            tab = tab,
                            binder = binder!!,
                            containerName = containerName,
                            isVisible = tab.id == activeTabId,
                            isMovingRight = isMovingRight,
                            hasMoved = hasMoved,
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
    isMovingRight: Boolean,
    hasMoved: Boolean,
    activity: Activity?,
    onSessionFinished: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val density = LocalDensity.current
    val defaultFontSizePx = remember { with(density) { 10.dp.roundToPx() } }
    val fontSizePx = TerminalSessionService.globalSessionList[tab.id]?.fontSizePx ?: defaultFontSizePx
    val onSurfaceColor = MaterialTheme.colorScheme.onSurface.toArgb()
    // Loaded once per composition - null = bundled font missing, fallback to system default
    val context = androidx.compose.ui.platform.LocalContext.current
    val terminalTypeface = remember { ResourcesCompat.getFont(context, R.font.jetbrains_mono) }

    val slideOffsetFraction = 0.08f
    val enterTransition = if (hasMoved) {
        slideInHorizontally(
            animationSpec = tween(durationMillis = 250, easing = FastOutSlowInEasing)
        ) { width ->
            if (isMovingRight) (width * slideOffsetFraction).toInt()
            else -(width * slideOffsetFraction).toInt()
        } + fadeIn(animationSpec = tween(250))
    } else {
        fadeIn(animationSpec = AnimationUtils.fastSpec())
    }

    val exitTransition = if (hasMoved) {
        slideOutHorizontally(
            animationSpec = tween(durationMillis = 250, easing = FastOutSlowInEasing)
        ) { width ->
            if (isMovingRight) -(width * slideOffsetFraction).toInt()
            else (width * slideOffsetFraction).toInt()
        } + fadeOut(animationSpec = tween(250))
    } else {
        fadeOut(animationSpec = AnimationUtils.fastSpec())
    }

    AnimatedVisibility(
        visible = isVisible,
        enter = enterTransition,
        exit = exitTransition,
        modifier = modifier
    ) {
        Column(modifier = Modifier.fillMaxSize()) {

            AndroidView(
                factory = { ctx ->
                    TerminalView(ctx, null).apply {
                        TerminalScreenState.terminalView = WeakReference(this)
                        setTextSize(fontSizePx)      // must run first — initializes mRenderer
                        setTypeface(terminalTypeface) // JetBrains Mono; null = system default
                        keepScreenOn = true
                        isFocusableInTouchMode = true

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
                        }

                        post {
                            requestFocus()
                            mEmulator?.mColors?.mCurrentColors?.apply {
                                set(256, onSurfaceColor)
                                set(258, onSurfaceColor)
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
                modifier = Modifier.fillMaxWidth().weight(1f)
            )

            AndroidView(
                factory = { ctx ->
                    VirtualKeysView(ctx, null).apply {
                        TerminalScreenState.virtualKeysView = WeakReference(this)
                        binder.getSession(tab.id)?.let { virtualKeysViewClient = VirtualKeysListener(it) }
                        buttonTextColor = onSurfaceColor
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
                        binder.getSession(tab.id)?.let { vkv.virtualKeysViewClient = VirtualKeysListener(it) }
                    }
                },
                modifier = Modifier
                    .fillMaxWidth()
                    .background(MaterialTheme.colorScheme.surfaceContainerHighest)
                    .windowInsetsPadding(WindowInsets.safeDrawing.only(WindowInsetsSides.Bottom))
                    .height(64.dp)
            )
        }
    }
}

@Composable
private fun UserPickerDialog(
    users: List<String>,
    onConfirm: (String) -> Unit,
    onDismiss: () -> Unit,
) {
    val context = LocalContext.current
    var selected by remember { mutableStateOf(users.firstOrNull() ?: "root") }
    val dialogShape = RoundedCornerShape(28.dp)

    androidx.compose.ui.window.Dialog(
        onDismissRequest = onDismiss,
        properties = androidx.compose.ui.window.DialogProperties(usePlatformDefaultWidth = false)
    ) {
        Surface(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 24.dp)
                .wrapContentHeight(),
            shape = dialogShape,
            color = MaterialTheme.colorScheme.surfaceContainer,
            border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.4f)),
            tonalElevation = 0.dp
        ) {
            Column(
                modifier = Modifier.padding(24.dp),
                verticalArrangement = Arrangement.spacedBy(16.dp)
            ) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    Icon(
                        imageVector = Icons.Default.Add,
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.primary,
                        modifier = Modifier.size(24.dp)
                    )
                    Text(
                        text = context.getString(R.string.open_terminal),
                        style = MaterialTheme.typography.titleLarge,
                        fontWeight = FontWeight.Bold
                    )
                }

                Text(
                    text = context.getString(R.string.select_user_to_enter),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.8f)
                )

                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .heightIn(max = 300.dp)
                        .verticalScroll(rememberScrollState()),
                    verticalArrangement = Arrangement.spacedBy(10.dp)
                ) {
                    users.forEach { user ->
                        val isSelected = user == selected
                        Surface(
                            onClick = { selected = user },
                            shape = RoundedCornerShape(16.dp),
                            color = if (isSelected) MaterialTheme.colorScheme.primary.copy(alpha = 0.08f) 
                                    else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.04f),
                            border = androidx.compose.foundation.BorderStroke(
                                1.dp, 
                                if (isSelected) MaterialTheme.colorScheme.primary.copy(alpha = 0.4f)
                                else MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.2f)
                            ),
                            tonalElevation = 0.dp
                        ) {
                            Row(
                                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 14.dp),
                                horizontalArrangement = Arrangement.SpaceBetween,
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Text(
                                    text = user,
                                    style = MaterialTheme.typography.bodyLarge,
                                    fontWeight = if (isSelected) FontWeight.SemiBold else FontWeight.Normal,
                                    color = if (isSelected) MaterialTheme.colorScheme.primary
                                            else MaterialTheme.colorScheme.onSurface
                                )
                                RadioButton(
                                    selected = isSelected,
                                    onClick = { selected = user },
                                    colors = RadioButtonDefaults.colors(
                                        selectedColor = MaterialTheme.colorScheme.primary,
                                        unselectedColor = MaterialTheme.colorScheme.outline.copy(alpha = 0.6f)
                                    )
                                )
                            }
                        }
                    }
                }

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.End,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    TextButton(
                        onClick = onDismiss,
                        shape = RoundedCornerShape(12.dp)
                    ) {
                        Text(
                            text = context.getString(android.R.string.cancel),
                            fontWeight = FontWeight.SemiBold,
                            color = MaterialTheme.colorScheme.primary.copy(alpha = 0.8f)
                        )
                    }
                    Spacer(modifier = Modifier.width(8.dp))
                    Button(
                        onClick = { onConfirm(selected) },
                        shape = RoundedCornerShape(16.dp),
                        elevation = ButtonDefaults.buttonElevation(0.dp, 0.dp, 0.dp)
                    ) {
                        Text(
                            text = context.getString(R.string.open),
                            fontWeight = FontWeight.Bold
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun CloseSessionDialog(
    onConfirm: () -> Unit,
    onDismiss: () -> Unit,
) {
    val context = LocalContext.current
    val dialogShape = RoundedCornerShape(28.dp)

    androidx.compose.ui.window.Dialog(
        onDismissRequest = onDismiss,
        properties = androidx.compose.ui.window.DialogProperties(usePlatformDefaultWidth = false)
    ) {
        Surface(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 24.dp)
                .wrapContentHeight(),
            shape = dialogShape,
            color = MaterialTheme.colorScheme.surfaceContainer,
            border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.4f)),
            tonalElevation = 0.dp
        ) {
            Column(
                modifier = Modifier.padding(24.dp),
                verticalArrangement = Arrangement.spacedBy(16.dp)
            ) {
                Text(
                    text = "Close this session?",
                    style = MaterialTheme.typography.titleLarge,
                    fontWeight = FontWeight.Bold,
                    color = MaterialTheme.colorScheme.onSurface
                )

                DialogFooterRow(
                    dismissLabel = context.getString(android.R.string.no),
                    confirmLabel = context.getString(android.R.string.yes),
                    onDismiss = onDismiss,
                    onConfirm = onConfirm,
                    confirmEnabled = true,
                    confirmColor = MaterialTheme.colorScheme.error,
                    confirmContentColor = MaterialTheme.colorScheme.onError
                )
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
