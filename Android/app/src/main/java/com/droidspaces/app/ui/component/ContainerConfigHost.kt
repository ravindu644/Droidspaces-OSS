package com.droidspaces.app.ui.component

import androidx.activity.compose.BackHandler
import com.droidspaces.app.util.AnimationUtils
import androidx.compose.animation.togetherWith
import androidx.compose.animation.fadeOut
import androidx.compose.animation.fadeIn
import androidx.compose.animation.AnimatedContent
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.consumeWindowInsets
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.ime
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.union
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.ScrollState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Check
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.saveable.rememberSaveableStateHolder
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.droidspaces.app.R
import com.droidspaces.app.ui.util.rememberClearFocus
import com.droidspaces.app.util.ContainerConfigState
import com.droidspaces.app.util.ContainerInfo
import com.droidspaces.app.util.GatewayErrors
import com.droidspaces.app.util.MacvlanErrors

enum class ConfigPage { Root, Network, Env, Privileged, Mounts }

/**
 * The container config screen and the pages it opens, shared by the create
 * wizard and the edit screen.
 *
 * The pages are not navigation destinations. Both screens keep the form in
 * plain remember, which a navigation push would dispose, taking every unsaved
 * edit with it. So the pages switch inside this one destination and edit the
 * same [state]. Every page edits a draft and hands it back on the way out,
 * unless the page's own rules fail, in which case leaving asks first. See
 * [DraftPageScaffold].
 */
@Composable
fun ContainerConfigHost(
    title: String,
    onBack: () -> Unit,
    bottomBar: @Composable () -> Unit,
    state: ContainerConfigState,
    onStateChange: (ContainerConfigState) -> Unit,
    installedContainers: List<ContainerInfo>,
    selfName: String,
    gatewayErrors: GatewayErrors,
    macvlanErrors: MacvlanErrors,
    collisionContainer: ContainerInfo?,
    leadingContent: @Composable ColumnScope.() -> Unit = {},
) {
    val clearFocus = rememberClearFocus()
    var page by rememberSaveable { mutableStateOf(ConfigPage.Root) }
    // Held here, not in the root page, so coming back from a page lands where the user left.
    val rootScroll = rememberScrollState()
    val open: (ConfigPage) -> Unit = { clearFocus(); page = it }
    val close = { clearFocus(); page = ConfigPage.Root }
    // The root leaves composition while a page shows, which would reset its
    // rememberSaveable state (the PIDs switch, an open confirm dialog) on every
    // round trip. The holder keeps it. Only the root: a page's draft starts
    // fresh each time it opens.
    val saved = rememberSaveableStateHolder()

    // The same crossfade the navigation graph gives every pushed screen (the
    // Settings and Auto Boot Priority pages), so a sub-page opens like one.
    AnimatedContent(
        targetState = page,
        transitionSpec = {
            fadeIn(animationSpec = AnimationUtils.fastSpec()) togetherWith
                fadeOut(animationSpec = AnimationUtils.fastSpec())
        },
        label = "config_page"
    ) { current ->
        // Only the page being shown handles back; one still fading out must not
        // take the press meant for the screen.
        CompositionLocalProvider(LocalPageActive provides (current == page)) {
            when (current) {
                ConfigPage.Root -> saved.SaveableStateProvider(ConfigPage.Root) {
                    ConfigPageScaffold(
                        title = title,
                        onBack = { clearFocus(); onBack() },
                        bottomBar = bottomBar,
                        scrollState = rootScroll
                    ) {
                        leadingContent()
                        ContainerConfigForm(
                            state = state,
                            onStateChange = onStateChange,
                            onOpenPage = open,
                            gatewayErrors = gatewayErrors,
                            macvlanErrors = macvlanErrors,
                            collisionContainer = collisionContainer
                        )
                    }
                }
                ConfigPage.Network -> NetworkModePage(
                    state = state,
                    // Only the network fields: the rest of the state may have been
                    // corrected for this kernel while the page was open.
                    onSave = { onStateChange(state.withNetworkOf(it)); close() },
                    onClose = close,
                    installedContainers = installedContainers,
                    selfName = selfName
                )
                ConfigPage.Env -> EnvironmentVariablesPage(
                    initial = state.envFileContent,
                    onSave = { onStateChange(state.copy(envFileContent = it)); close() },
                    onClose = close
                )
                ConfigPage.Privileged -> PrivilegedModePage(
                    initial = state.privileged,
                    onApply = { onStateChange(state.copy(privileged = it)); close() },
                    onClose = close
                )
                ConfigPage.Mounts -> BindMountsPage(
                    mounts = state.bindMounts,
                    onSave = { onStateChange(state.copy(bindMounts = it)); close() },
                    onClose = close
                )
            }
        }
    }
}

/** Whether the page reading it is the one on screen, rather than one fading out. */
val LocalPageActive = staticCompositionLocalOf { true }

/**
 * Top bar, optional bottom bar and a scrolling body, with the keyboard handled:
 * the body pads for the IME (less whatever the bottom bar already covers), so
 * a focused field scrolls into view above it. Without a bottom bar the body
 * also clears the navigation bar itself. Set [scrollable] false when the body
 * fills the height with a weighted child instead.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ConfigPageScaffold(
    title: String,
    onBack: () -> Unit,
    bottomBar: (@Composable () -> Unit)? = null,
    scrollable: Boolean = true,
    scrollState: ScrollState = rememberScrollState(),
    content: @Composable ColumnScope.() -> Unit,
) {
    val context = LocalContext.current
    Scaffold(
        containerColor = Color.Transparent,
        topBar = {
            TopAppBar(
                title = {
                    Text(
                        title,
                        style = MaterialTheme.typography.titleLarge,
                        fontWeight = FontWeight.Bold,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = context.getString(R.string.back))
                    }
                },
                colors = TopAppBarDefaults.topAppBarColors(containerColor = Color.Transparent),
                expandedHeight = if (isShortHeight()) 48.dp else TopAppBarDefaults.TopAppBarExpandedHeight,
                windowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal)
            )
        },
        bottomBar = { bottomBar?.invoke() },
        contentWindowInsets = WindowInsets(0)
    ) { inner ->
        Box(
            modifier = Modifier
                .padding(inner)
                .consumeWindowInsets(inner)
                .windowInsetsPadding(
                    if (bottomBar == null) WindowInsets.ime.union(WindowInsets.navigationBars) else WindowInsets.ime
                )
        ) {
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .then(if (scrollable) Modifier.verticalScroll(scrollState) else Modifier)
            ) {
                content()
                if (scrollable) Spacer(Modifier.height(24.dp))
            }
        }
    }
}

/**
 * The shell every config page shares: the page edits a draft, and leaving it
 * keeps the draft when [canSave] says it would save. When it would not, back,
 * from the arrow or the system, asks "Discard edits?" instead, so no page can
 * leave broken settings behind.
 *
 * [confirmLabel] is for the one page whose edits need an explicit go-ahead
 * (privileged mode): it gets a bar with that button, grey until something
 * changed and the page's rules pass, and back never keeps its draft.
 */
@Composable
fun DraftPageScaffold(
    title: String,
    dirty: Boolean,
    canSave: Boolean,
    onSave: () -> Unit,
    onClose: () -> Unit,
    scrollable: Boolean = true,
    confirmLabel: String? = null,
    content: @Composable ColumnScope.() -> Unit,
) {
    val clearFocus = rememberClearFocus()
    val back = rememberDraftBack(
        dirty = dirty,
        keep = if (confirmLabel == null && canSave) onSave else null,
        explicit = confirmLabel != null,
        onClose = onClose
    )
    ConfigPageScaffold(
        title = title,
        onBack = { clearFocus(); back() },
        scrollable = scrollable,
        bottomBar = confirmLabel?.let { label ->
            {
                PrimaryActionBottomBar(
                    label = label,
                    icon = Icons.Default.Check,
                    onClick = { clearFocus(); onSave() },
                    enabled = dirty && canSave,
                    horizontalPadding = screenGutter()
                )
            }
        },
        content = content
    )
}

/**
 * Asked when leaving a page whose edits will not be kept: [explicit] for a
 * page that was not applied with its button, otherwise one whose settings
 * break its rules.
 */
@Composable
fun DiscardEditsDialog(explicit: Boolean, onDiscard: () -> Unit, onKeepEditing: () -> Unit) {
    val context = LocalContext.current
    DsDialog(
        onDismiss = onKeepEditing,
        footer = {
            DialogFooterRow(
                dismissLabel = context.getString(R.string.keep_editing),
                confirmLabel = context.getString(R.string.discard),
                onDismiss = onKeepEditing,
                onConfirm = onDiscard,
                destructive = true
            )
        }
    ) {
        Text(
            context.getString(R.string.discard_edits_title),
            style = MaterialTheme.typography.titleLarge,
            fontWeight = FontWeight.Bold
        )
        Text(
            context.getString(if (explicit) R.string.discard_edits_body else R.string.discard_invalid_body),
            style = MaterialTheme.typography.bodyMedium
        )
    }
}

/**
 * Back for a page with a draft: close when nothing changed, [keep] the draft
 * when it can be kept, otherwise ask with [DiscardEditsDialog]. Returns the
 * function the top bar's arrow calls, so both paths behave the same.
 */
@Composable
fun rememberDraftBack(dirty: Boolean, keep: (() -> Unit)?, explicit: Boolean, onClose: () -> Unit): () -> Unit {
    var asking by rememberSaveable { mutableStateOf(false) }
    val back = {
        when {
            !dirty -> onClose()
            keep != null -> keep()
            else -> asking = true
        }
    }
    BackHandler(enabled = LocalPageActive.current, onBack = back)
    if (asking) {
        DiscardEditsDialog(
            explicit = explicit,
            onDiscard = { asking = false; onClose() },
            onKeepEditing = { asking = false }
        )
    }
    return back
}
