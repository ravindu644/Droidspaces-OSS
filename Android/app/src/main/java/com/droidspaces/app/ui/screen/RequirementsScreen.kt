package com.droidspaces.app.ui.screen

import android.content.ClipData
import android.content.ClipboardManager
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.animation.animateContentSize
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.ContentCopy
import androidx.compose.material.icons.filled.ExpandMore
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.droidspaces.app.R
import com.droidspaces.app.ui.component.ContainerLogViewer
import com.droidspaces.app.ui.viewmodel.AppStateViewModel
import com.droidspaces.app.util.Constants
import com.droidspaces.app.util.ContainerOperationExecutor
import com.droidspaces.app.util.ViewModelLogger
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import androidx.compose.runtime.rememberCoroutineScope
import com.droidspaces.app.ui.util.showSuccess

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun RequirementsScreen(
    onNavigateBack: () -> Unit
) {
    val context = LocalContext.current
    val appStateViewModel: AppStateViewModel = viewModel()
    val isRootAvailable = appStateViewModel.isRootAvailable
    val scope = rememberCoroutineScope()
    val snackbarHostState = remember { SnackbarHostState() }

    // Console state management
    var showLogViewer by remember { mutableStateOf(false) }
    var checkLogs by remember { mutableStateOf(androidx.compose.runtime.mutableStateListOf<Pair<Int, String>>()) }
    var isCheckRunning by remember { mutableStateOf(false) }

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Text(
                        text = context.getString(R.string.requirements),
                        style = MaterialTheme.typography.titleLarge,
                        fontWeight = FontWeight.SemiBold
                    )
                },
                navigationIcon = {
                    IconButton(onClick = onNavigateBack) {
                        Icon(
                            imageVector = Icons.AutoMirrored.Filled.ArrowBack,
                            contentDescription = context.getString(R.string.back)
                        )
                    }
                }
            )
        }
    ) { padding ->
        Box(modifier = Modifier.fillMaxSize()) {
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(padding)
                    .verticalScroll(rememberScrollState())
                    .padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(16.dp)
            ) {
                // Expandable Kernel Requirements Section
                ExpandableKernelRequirementsSection(
                    snackbarHostState = snackbarHostState
                )

                // Check Requirements Button
                CheckRequirementsButton(
                    isRootAvailable = isRootAvailable,
                    isRunning = isCheckRunning,
                    onClick = {
                    scope.launch {
                        isCheckRunning = true
                        checkLogs.clear()
                        showLogViewer = true

                        // Create logger
                        val logger = ViewModelLogger { level, message ->
                            checkLogs.add(level to message)
                        }.apply {
                            verbose = true
                        }

                        try {
                            // Get droidspaces command
                            val droidspacesCmd = withContext(Dispatchers.IO) {
                                Constants.getDroidspacesCommand()
                            }

                            // Execute check command
                            val command = "$droidspacesCmd check"
                            val success = ContainerOperationExecutor.executeCommand(
                                command = command,
                                operation = "check",
                                logger = logger,
                                skipHeader = true // Skip "Starting check operation..." message
                            )

                            // Add newline before completion message
                            if (success) {
                                logger.i("")
                                logger.i(context.getString(R.string.requirements_check_completed_successfully))
                            } else {
                                logger.e("")
                                logger.e(context.getString(R.string.requirements_check_completed_with_errors))
                            }
                        } catch (e: Exception) {
                            logger.e("Exception: ${e.message}")
                            logger.e(e.stackTraceToString())
                        } finally {
                            isCheckRunning = false
                        }
                    }
                }
            )
            }

            // Snackbar host
            SnackbarHost(
                hostState = snackbarHostState,
                modifier = Modifier.align(Alignment.BottomCenter)
            )
        }
    }

    // Console viewer dialog
    if (showLogViewer) {
        ContainerLogViewer(
            containerName = context.getString(R.string.requirements_check_title),
            logs = checkLogs.toList(),
            onDismiss = {
                showLogViewer = false
            },
            onClear = {
                checkLogs.clear()
            },
            isBlocking = isCheckRunning
        )
    }
}

/**
 * Expandable Kernel Requirements Section - like HTML <details> tag
 */
@Composable
private fun ExpandableKernelRequirementsSection(
    snackbarHostState: SnackbarHostState
) {
    val context = LocalContext.current
    var isExpanded by remember { mutableStateOf(false) }

    val rotationAngle by animateFloatAsState(
        targetValue = if (isExpanded) 180f else 0f,
        animationSpec = tween(
            durationMillis = 200,
            easing = FastOutSlowInEasing
        ),
        label = "chevron_rotation"
    )

    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(16.dp),
        color = MaterialTheme.colorScheme.surfaceContainerHigh,
        tonalElevation = 1.dp
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .animateContentSize(
                    animationSpec = tween(
                        durationMillis = 200,
                        easing = FastOutSlowInEasing
                    )
                )
        ) {
            // Clickable header
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .clickable(onClick = { isExpanded = !isExpanded })
                    .padding(16.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = context.getString(R.string.kernel_requirements),
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Bold
                )
                Icon(
                    imageVector = Icons.Default.ExpandMore,
                    contentDescription = null,
                    modifier = Modifier.rotate(rotationAngle),
                    tint = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }

            // Expandable content
            if (isExpanded) {
                CodeBox(
                    code = """# Minimal Droidspaces Support
# Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>

# IPC mechanisms (required for tools that rely on shared memory and IPC namespaces)
CONFIG_SYSCTL=y
CONFIG_SYSVIPC=y
CONFIG_POSIX_MQUEUE=y

# Core namespace support (essential for isolation and running init systems)
CONFIG_NAMESPACES=y
CONFIG_PID_NS=y
CONFIG_UTS_NS=y
CONFIG_IPC_NS=y
CONFIG_USER_NS=y

# Seccomp support (enables syscall filtering and security hardening)
CONFIG_SECCOMP=y
CONFIG_SECCOMP_FILTER=y

# Control groups support (required for systemd and resource accounting)
CONFIG_CGROUPS=y
CONFIG_CGROUP_DEVICE=y
CONFIG_CGROUP_PIDS=y
CONFIG_MEMCG=y

# Device filesystem support (enables hardware access when --hw-access is enabled)
CONFIG_DEVTMPFS=y

# Overlay filesystem support (required for volatile mode)
CONFIG_OVERLAY_FS=y

# Firmware loading support (optional, used when --hw-access is enabled)
CONFIG_FW_LOADER=y
CONFIG_FW_LOADER_USER_HELPER=y
CONFIG_FW_LOADER_COMPRESS=y

# Disable this on older kernels to make internet work
CONFIG_ANDROID_PARANOID_NETWORK=n""",
                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
                    snackbarHostState = snackbarHostState
                )
            }
        }
    }
}

/**
 * Code box component with copy functionality - no word wrap, horizontal scrolling enabled
 */
@Composable
private fun CodeBox(
    code: String,
    modifier: Modifier = Modifier,
    snackbarHostState: SnackbarHostState
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val horizontalScrollState = rememberScrollState()

    Surface(
        modifier = modifier.fillMaxWidth(),
        shape = RoundedCornerShape(12.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 0.dp
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            // Code content - no word wrap, horizontal scrolling
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .horizontalScroll(horizontalScrollState)
            ) {
                androidx.compose.material3.ProvideTextStyle(
                    MaterialTheme.typography.bodySmall.copy(fontFamily = FontFamily.Monospace)
                ) {
                    Text(
                        text = code,
                        style = MaterialTheme.typography.bodySmall.copy(fontFamily = FontFamily.Monospace),
                        color = MaterialTheme.colorScheme.onSurface,
                        softWrap = false, // No word wrap - allows horizontal scrolling
                        modifier = Modifier.wrapContentWidth() // Allow text to be as wide as needed
                    )
                }
            }

            // Copy button - matching "Copy login" button style
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.End
            ) {
                Button(
                    onClick = {
                        val clipboard = context.getSystemService(ClipboardManager::class.java)
                        val clip = ClipData.newPlainText(
                            context.getString(R.string.kernel_requirements_clipboard_label),
                            code
                        )
                        clipboard.setPrimaryClip(clip)
                        // Show snackbar feedback
                        scope.showSuccess(snackbarHostState, context.getString(R.string.kernel_requirements_copied))
                    },
                    modifier = Modifier.widthIn(min = 140.dp),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = MaterialTheme.colorScheme.primary,
                        contentColor = MaterialTheme.colorScheme.onPrimary
                    )
                ) {
                    Icon(
                        imageVector = Icons.Default.ContentCopy,
                        contentDescription = context.getString(R.string.copy),
                        modifier = Modifier.size(18.dp)
                    )
                    Spacer(Modifier.width(6.dp))
                    Text(
                        text = context.getString(R.string.copy),
                        fontWeight = FontWeight.SemiBold
                    )
                }
            }
        }
    }
}

/**
 * Check Requirements Button - runs droidspaces check command
 */
@Composable
private fun CheckRequirementsButton(
    isRootAvailable: Boolean,
    isRunning: Boolean,
    onClick: () -> Unit
) {
    val context = LocalContext.current

    Button(
        onClick = onClick,
        enabled = isRootAvailable && !isRunning,
        modifier = Modifier.fillMaxWidth(),
        colors = ButtonDefaults.buttonColors(
            containerColor = MaterialTheme.colorScheme.primary,
            contentColor = MaterialTheme.colorScheme.onPrimary,
            disabledContainerColor = MaterialTheme.colorScheme.surfaceContainerHighest,
            disabledContentColor = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.38f)
        )
    ) {
        Text(
            text = context.getString(R.string.check_requirements),
            fontWeight = FontWeight.SemiBold
        )
    }
}

