package com.droidspaces.app.ui.component

import android.content.Intent
import android.net.Uri
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.OpenInNew
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.SystemUpdate
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.draw.clip
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
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

    val accentColor = when {
        isWorking -> MaterialTheme.colorScheme.primary
        status == DroidspacesStatus.UpdateAvailable -> MaterialTheme.colorScheme.tertiary
        else -> MaterialTheme.colorScheme.error
    }
    val isError = !isWorking

    val cardShape = RoundedCornerShape(20.dp)
 
    Surface(
        onClick = onClick,
        modifier = Modifier.fillMaxWidth().clip(cardShape),
        shape = cardShape,
        color = MaterialTheme.colorScheme.surfaceContainer,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f))
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
                // Left side: Status Beacon + Status Label
                Row(
                    modifier = Modifier.weight(1f),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(10.dp)
                ) {
                    Surface(
                        modifier = Modifier.size(10.dp),
                        shape = CircleShape,
                        color = if (isChecking) MaterialTheme.colorScheme.outline else accentColor
                    ) {}
                    Text(
                        text = when {
                            !isRootAvailable -> context.getString(R.string.root_unavailable)
                            kernelBlocked -> context.getString(R.string.kernel_unsupported)
                            isChecking -> context.getString(R.string.backend_checking)
                            isWorking -> context.getString(R.string.backend_installed)
                            status == DroidspacesStatus.UpdateAvailable -> context.getString(R.string.backend_update_available)
                            else -> context.getString(R.string.backend_attention_required)
                        }.uppercase(),
                        style = MaterialTheme.typography.labelLarge,
                        fontWeight = FontWeight.Black,
                        letterSpacing = 0.5.sp,
                        color = accentColor,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }

                // Right side: mode pill
                if (isWorking && backendMode != null) {
                    // The mode arrives as DAEMON or DIRECT, which AppStateViewModel compares
                    // against, so only the chip turns it into title case.
                    StatusChip(
                        label = backendMode!!.lowercase().replaceFirstChar { it.titlecase() },
                        color = accentColor,
                        active = true
                    )
                }
            }

            HorizontalDivider(
                modifier = Modifier.fillMaxWidth(),
                color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.3f)
            )

            // Wordmark and Version block
            Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
                Text(
                    text = when {
                        isWorking || status == DroidspacesStatus.UpdateAvailable -> "DROIDSPACES"
                        !isRootAvailable -> context.getString(R.string.root_unavailable)
                        kernelBlocked -> context.getString(R.string.kernel_unsupported)
                        status == DroidspacesStatus.NotInstalled -> context.getString(R.string.backend_not_installed)
                        else -> context.getString(R.string.backend_corrupted)
                    },
                    style = MaterialTheme.typography.titleLarge,
                    fontWeight = FontWeight.Black,
                    letterSpacing = (-0.5).sp
                )

                if (isWorking) {
                    Text(
                        text = "${context.getString(R.string.version).uppercase()}: ${droidspacesVersion ?: "---"}",
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f),
                        fontWeight = FontWeight.Bold,
                        letterSpacing = 0.5.sp,
                        modifier = Modifier.padding(top = 2.dp)
                    )
                }
            }

            // Error banner
            if (isError) {
                Surface(
                    modifier = Modifier.fillMaxWidth(),
                    shape = RoundedCornerShape(10.dp),
                    color = accentColor.copy(alpha = 0.1f)
                ) {
                    Row(
                        modifier = Modifier.padding(10.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Icon(
                            imageVector = Icons.Default.Info,
                            contentDescription = null,
                            tint = accentColor,
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
                            style = MaterialTheme.typography.bodySmall,
                            fontWeight = FontWeight.Medium,
                            color = accentColor
                        )
                    }
                }
            }

            // App update banner, same slot and shape as the error banner above but
            // in tertiary, the "something to fetch" accent. Slides in once the
            // release check lands. The APK is never fetched in-app: this app holds
            // a root shell, so an APK download path is a supply-chain surface we
            // do not want. The row opens the GitHub release page instead.
            AnimatedVisibility(
                visible = appUpdate != null,
                enter = expandVertically(expandFrom = Alignment.Top) + fadeIn(),
                exit = shrinkVertically(shrinkTowards = Alignment.Top) + fadeOut()
            ) {
                val update = appUpdate ?: return@AnimatedVisibility
                val updateColor = MaterialTheme.colorScheme.tertiary
                Surface(
                    onClick = {
                        context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(update.releaseUrl)))
                    },
                    modifier = Modifier.fillMaxWidth(),
                    shape = RoundedCornerShape(10.dp),
                    color = updateColor.copy(alpha = 0.1f)
                ) {
                    Row(
                        modifier = Modifier.padding(10.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Icon(
                            imageVector = Icons.Default.SystemUpdate,
                            contentDescription = null,
                            tint = updateColor,
                            modifier = Modifier.size(16.dp)
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(
                            text = context.getString(R.string.app_update_message, update.version),
                            style = MaterialTheme.typography.bodySmall,
                            fontWeight = FontWeight.Medium,
                            color = updateColor,
                            modifier = Modifier.weight(1f)
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Icon(
                            imageVector = Icons.AutoMirrored.Filled.OpenInNew,
                            contentDescription = null,
                            tint = updateColor,
                            modifier = Modifier.size(16.dp)
                        )
                    }
                }
            }
        }
    }
}
