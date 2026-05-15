package com.droidspaces.app.ui.screen

import android.content.ClipData
import android.content.ClipboardManager
import android.widget.Toast
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
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.ChevronRight
import androidx.compose.material.icons.automirrored.filled.OpenInNew
import android.content.Intent
import android.net.Uri
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import com.droidspaces.app.ui.theme.JetBrainsMono
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.graphics.Color
import androidx.compose.foundation.BorderStroke
import androidx.lifecycle.viewmodel.compose.viewModel
import com.droidspaces.app.R
import com.droidspaces.app.ui.component.TerminalDialog
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
                // non-GKI Kernel Requirements Section
                ExpandableKernelRequirementsSection(
                    title = context.getString(R.string.kernel_requirements_nongki),
                    code = """# Kernel configurations for full DroidSpaces support for non-GKI
# Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>

# IPC mechanisms
CONFIG_SYSCTL=y
CONFIG_SYSVIPC=y
CONFIG_POSIX_MQUEUE=y

# Core namespace support
CONFIG_NAMESPACES=y
CONFIG_PID_NS=y
CONFIG_UTS_NS=y
CONFIG_IPC_NS=y

# Seccomp support
CONFIG_SECCOMP=y
CONFIG_SECCOMP_FILTER=y

# Control groups support
CONFIG_CGROUPS=y
CONFIG_CGROUP_DEVICE=y
CONFIG_CGROUP_PIDS=y
CONFIG_MEMCG=y
CONFIG_CGROUP_SCHED=y
CONFIG_FAIR_GROUP_SCHED=y
CONFIG_CGROUP_FREEZER=y
CONFIG_CGROUP_NET_PRIO=y

# Device filesystem support
CONFIG_DEVTMPFS=y

# Overlay filesystem support (required for volatile mode)
CONFIG_OVERLAY_FS=y

# Enable xattr support on tmpfs
# (required for NixOS setcap wrappers in /run/wrappers)
CONFIG_TMPFS_XATTR=y

# Firmware loading support
CONFIG_FW_LOADER=y
CONFIG_FW_LOADER_USER_HELPER=y
CONFIG_FW_LOADER_COMPRESS=y

# Droidspaces Network Isolation Support - NAT/none modes
CONFIG_NET_NS=y
CONFIG_VETH=y
CONFIG_BRIDGE=y
CONFIG_NETFILTER=y
CONFIG_BRIDGE_NETFILTER=y
CONFIG_NETFILTER_ADVANCED=y
CONFIG_NF_CONNTRACK=y
CONFIG_IP_NF_IPTABLES=y
CONFIG_IP_NF_FILTER=y
CONFIG_NF_NAT=y
CONFIG_NF_TABLES=y
CONFIG_IP_NF_TARGET_MASQUERADE=y
CONFIG_NETFILTER_XT_TARGET_MASQUERADE=y
CONFIG_NETFILTER_XT_TARGET_TCPMSS=y
CONFIG_NETFILTER_XT_MATCH_ADDRTYPE=y
CONFIG_NF_CONNTRACK_NETLINK=y
CONFIG_NF_NAT_REDIRECT=y
CONFIG_IP_ADVANCED_ROUTER=y
CONFIG_IP_MULTIPLE_TABLES=y

# legacy compat
CONFIG_NF_CONNTRACK_IPV4=y
CONFIG_NF_NAT_IPV4=y
CONFIG_IP_NF_NAT=y

# Disable this on older kernels to make internet work
CONFIG_ANDROID_PARANOID_NETWORK=n""",
                    guideUrl = "https://github.com/ravindu644/Droidspaces-OSS/blob/main/Documentation/Kernel-Configuration.md#non-gki",
                    snackbarHostState = snackbarHostState
                )

                // GKI Kernel Requirements Section
                ExpandableKernelRequirementsSection(
                    title = context.getString(R.string.kernel_requirements_gki),
                    code = """# Kernel configurations for full DroidSpaces support for GKI
# Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>

# NOTE: enabling these configs are not enough, additional kernel patches needed for GKI.
# Guide: https://github.com/ravindu644/Droidspaces-OSS/blob/main/Documentation/Kernel-Configuration.md#configuring-gki-kernels

# IPC
CONFIG_SYSVIPC=y
CONFIG_POSIX_MQUEUE=y

# Namespaces
CONFIG_IPC_NS=y
CONFIG_PID_NS=y

# HW Access Support
CONFIG_DEVTMPFS=y

# --- Below configs are optional but recommended ---

# Networking (Docker/NAT support)
CONFIG_NETFILTER_XT_MATCH_ADDRTYPE=y

# UFW support
CONFIG_NETFILTER_XT_TARGET_REJECT=y
CONFIG_NETFILTER_XT_TARGET_LOG=y
CONFIG_NETFILTER_XT_MATCH_RECENT=y

# Fail2ban support
CONFIG_IP_SET=y
CONFIG_IP_SET_HASH_IP=y
CONFIG_IP_SET_HASH_NET=y
CONFIG_NETFILTER_XT_SET=y

# Enable xattr support on tmpfs
# (required for NixOS setcap wrappers in /run/wrappers)
CONFIG_TMPFS_XATTR=y""",
                    guideUrl = "https://github.com/ravindu644/Droidspaces-OSS/blob/main/Documentation/Kernel-Configuration.md#configuring-gki-kernels",
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
        TerminalDialog(
            title = context.getString(R.string.requirements_check_title),
            logs = checkLogs.toList(),
            onDismiss = {
                showLogViewer = false
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
    title: String,
    code: String,
    guideUrl: String? = null,
    snackbarHostState: SnackbarHostState
) {
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
        shape = RoundedCornerShape(20.dp),
        color = MaterialTheme.colorScheme.surfaceContainer,
        border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f))
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
                    text = title,
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
                    code = code,
                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
                    guideUrl = guideUrl,
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
    guideUrl: String? = null,
    snackbarHostState: SnackbarHostState
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val horizontalScrollState = rememberScrollState()

    Surface(
        modifier = modifier.fillMaxWidth(),
        shape = RoundedCornerShape(20.dp),
        color = MaterialTheme.colorScheme.surfaceContainerHighest, // Level up for depth
        border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.3f))
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
                    MaterialTheme.typography.bodySmall.copy(fontFamily = JetBrainsMono)
                ) {
                    Text(
                        text = code,
                        style = MaterialTheme.typography.bodySmall.copy(fontFamily = JetBrainsMono),
                        color = MaterialTheme.colorScheme.onSurface,
                        softWrap = false, // No word wrap - allows horizontal scrolling
                        modifier = Modifier.wrapContentWidth() // Allow text to be as wide as needed
                    )
                }
            }

            // Copy button - matching "Copy login" button style
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.End,
                verticalAlignment = Alignment.CenterVertically
            ) {
                if (guideUrl != null) {
                    TextButton(
                        onClick = {
                            try {
                                val intent = Intent(Intent.ACTION_VIEW, Uri.parse(guideUrl))
                                context.startActivity(intent)
                            } catch (e: Exception) {
                                Toast.makeText(context, "Failed to open link", Toast.LENGTH_SHORT).show()
                            }
                        },
                        modifier = Modifier.padding(end = 8.dp)
                    ) {
                        Icon(
                            imageVector = Icons.AutoMirrored.Filled.OpenInNew,
                            contentDescription = null,
                            modifier = Modifier.size(18.dp)
                        )
                        Spacer(Modifier.width(6.dp))
                        Text(
                            text = context.getString(R.string.guide),
                            fontWeight = FontWeight.SemiBold
                        )
                    }
                }

                Button(
                    onClick = {
                        val clipboard = context.getSystemService(ClipboardManager::class.java)
                        val clip = ClipData.newPlainText(
                            context.getString(R.string.kernel_requirements),
                            code
                        )
                        clipboard.setPrimaryClip(clip)
                        Toast.makeText(context, R.string.kernel_requirements_copied, Toast.LENGTH_SHORT).show()
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
    val shape = RoundedCornerShape(20.dp)
    val alpha = if (isRootAvailable && !isRunning) 1f else 0.5f

    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .height(54.dp)
            .alpha(alpha)
            .clip(shape)
            .clickable(enabled = isRootAvailable && !isRunning, onClick = onClick),
        shape = shape,
        color = if (isRootAvailable && !isRunning) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.surfaceContainerHigh,
        tonalElevation = 0.dp
    ) {
        Row(
            modifier = Modifier.fillMaxSize(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.Center
        ) {
            if (isRunning) {
                CircularProgressIndicator(
                    modifier = Modifier.size(20.dp),
                    strokeWidth = 2.dp,
                    color = MaterialTheme.colorScheme.onPrimary
                )
            } else {
                Icon(
                    imageVector = Icons.Default.Search,
                    contentDescription = null,
                    modifier = Modifier.size(20.dp),
                    tint = if (isRootAvailable) MaterialTheme.colorScheme.onPrimary else MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.4f)
                )
            }
            Spacer(modifier = Modifier.width(10.dp))
            Text(
                text = context.getString(R.string.check_requirements),
                style = MaterialTheme.typography.labelLarge,
                fontWeight = FontWeight.Bold,
                color = if (isRootAvailable && !isRunning) MaterialTheme.colorScheme.onPrimary else MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.4f)
            )
            if (!isRunning) {
                Spacer(modifier = Modifier.width(6.dp))
                Icon(
                    imageVector = Icons.Default.ChevronRight,
                    contentDescription = null,
                    modifier = Modifier.size(18.dp),
                    tint = if (isRootAvailable) MaterialTheme.colorScheme.onPrimary.copy(alpha = 0.7f) else MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.4f)
                )
            }
        }
    }
}

