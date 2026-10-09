package com.droidspaces.app.ui.component

import android.content.Intent
import android.net.Uri
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.OpenInNew
import androidx.compose.material.icons.filled.Error
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.SystemUpdate
import androidx.compose.material.icons.filled.Update
import androidx.compose.material.icons.filled.Verified
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.droidspaces.app.ui.util.LoadingIndicator
import com.droidspaces.app.ui.util.LoadingSize
import com.droidspaces.app.util.AppUpdateInfo
import com.droidspaces.app.util.SystemInfoManager
import com.droidspaces.app.R

enum class DroidspacesStatus {
    Working,
    UpdateAvailable,
    NotInstalled,
    Corrupted,
    ModuleMissing
}

@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
fun DroidspacesStatusCard(
    status: DroidspacesStatus,
    version: String? = null,
    isChecking: Boolean = false,
    isRootAvailable: Boolean = true,
    refreshTrigger: Int = 0,
    appUpdate: AppUpdateInfo? = null,
    isKernelSupported: Boolean = true,
    onClick: () -> Unit = {}
) {
    val context = LocalContext.current
    val isWorking = isRootAvailable && isKernelSupported && status == DroidspacesStatus.Working
    // Backend fine, kernel failed a MUST HAVE probe. Rendered like the root
    // state: no wordmark, no version, no mode pill, the whole card is the link.
    val kernelBlocked = isRootAvailable && !isKernelSupported && status == DroidspacesStatus.Working

    var droidspacesVersion by remember {
        mutableStateOf(version ?: SystemInfoManager.getCachedDroidspacesVersion(context))
    }
    var backendMode by remember {
        mutableStateOf(if (isWorking) SystemInfoManager.getCachedBackendMode(context) else null)
    }

    LaunchedEffect(status, isRootAvailable, isKernelSupported, refreshTrigger) {
        if (isWorking) {
            droidspacesVersion = SystemInfoManager.getDroidspacesVersion(context)
            backendMode = SystemInfoManager.getBackendMode(context)
        } else {
            backendMode = null
        }
    }

    // The card itself carries the state: primaryContainer when healthy (the one
    // highlighted surface on Home), tertiaryContainer for an update, errorContainer
    // for anything that needs the user. Checking keeps the healthy colour so a pull
    // to refresh does not flash the card.
    val scheme = MaterialTheme.colorScheme
    val updateOnly = isRootAvailable && isKernelSupported && status == DroidspacesStatus.UpdateAvailable
    val containerColor = when {
        isWorking || isChecking -> scheme.primaryContainer
        updateOnly -> scheme.tertiaryContainer
        else -> scheme.errorContainer
    }
    val isError = !isWorking

    Surface(
        onClick = onClick,
        modifier = Modifier.fillMaxWidth(),
        shape = MaterialTheme.shapes.extraLarge,
        color = containerColor
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(CardContentPadding),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth().height(CardHeaderHeight),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Row(
                    modifier = Modifier.weight(1f),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    when {
                        isChecking -> LoadingIndicator(size = LoadingSize.Medium, color = LocalContentColor.current)
                        else -> Icon(
                            imageVector = when {
                                isWorking -> Icons.Default.Verified
                                updateOnly -> Icons.Default.Update
                                else -> Icons.Default.Error
                            },
                            contentDescription = null,
                            modifier = Modifier.size(24.dp)
                        )
                    }
                    Text(
                        text = when {
                            !isRootAvailable -> context.getString(R.string.root_unavailable)
                            kernelBlocked -> context.getString(R.string.kernel_unsupported)
                            isChecking -> context.getString(R.string.backend_checking)
                            isWorking -> context.getString(R.string.backend_installed)
                            status == DroidspacesStatus.UpdateAvailable -> context.getString(R.string.backend_update_available)
                            else -> context.getString(R.string.backend_attention_required)
                        },
                        style = MaterialTheme.typography.titleMediumEmphasized,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }

                if (isWorking && backendMode != null) {
                    // The mode arrives as DAEMON or DIRECT, which AppStateViewModel compares
                    // against, so only the chip turns it into title case. The card is already
                    // primaryContainer, so the chip steps up to primary to stay visible.
                    StatusChip(
                        label = backendMode!!.lowercase().replaceFirstChar { it.titlecase() },
                        color = scheme.onPrimary,
                        containerColor = scheme.primary
                    )
                }
            }

            HorizontalDivider()

            Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
                Text(
                    text = when {
                        isWorking || status == DroidspacesStatus.UpdateAvailable -> "Droidspaces"
                        !isRootAvailable -> context.getString(R.string.root_unavailable)
                        kernelBlocked -> context.getString(R.string.kernel_unsupported)
                        status == DroidspacesStatus.NotInstalled -> context.getString(R.string.backend_not_installed)
                        else -> context.getString(R.string.backend_corrupted)
                    },
                    style = MaterialTheme.typography.titleLargeEmphasized
                )

                if (isWorking) {
                    Text(
                        text = "${context.getString(R.string.version)}: ${droidspacesVersion ?: "---"}",
                        style = MaterialTheme.typography.bodyMedium
                    )
                }
            }

            // What to do about it. The card's own colour already says something is wrong,
            // so this is plain text on it rather than a second tinted box.
            if (isError) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Icon(
                        imageVector = Icons.Default.Info,
                        contentDescription = null,
                        modifier = Modifier.size(16.dp)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = when {
                            !isRootAvailable -> context.getString(R.string.grant_root_message)
                            kernelBlocked -> context.getString(R.string.kernel_unsupported_message)
                            status == DroidspacesStatus.UpdateAvailable -> context.getString(R.string.update_available_message)
                            else -> context.getString(R.string.tap_to_fix_system)
                        },
                        style = MaterialTheme.typography.bodySmall
                    )
                }
            }

            // App update, a box inside the card in tertiaryContainer, the "something to
            // fetch" accent. Slides in once the release check lands. The APK is never
            // fetched in-app: this app holds a root shell, so an APK download path is a
            // supply-chain surface we do not want. The row opens the GitHub release page.
            AnimatedVisibility(
                visible = appUpdate != null,
                enter = expandVertically(expandFrom = Alignment.Top) + fadeIn(),
                exit = shrinkVertically(shrinkTowards = Alignment.Top) + fadeOut()
            ) {
                val update = appUpdate ?: return@AnimatedVisibility
                Surface(
                    onClick = {
                        context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(update.releaseUrl)))
                    },
                    modifier = Modifier.fillMaxWidth(),
                    shape = MaterialTheme.shapes.large,
                    color = scheme.tertiaryContainer
                ) {
                    Row(
                        modifier = Modifier.padding(16.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Icon(
                            imageVector = Icons.Default.SystemUpdate,
                            contentDescription = null,
                            modifier = Modifier.size(16.dp)
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(
                            text = context.getString(R.string.app_update_message, update.version),
                            style = MaterialTheme.typography.bodySmall,
                            modifier = Modifier.weight(1f)
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Icon(
                            imageVector = Icons.AutoMirrored.Filled.OpenInNew,
                            contentDescription = null,
                            modifier = Modifier.size(16.dp)
                        )
                    }
                }
            }
        }
    }
}
