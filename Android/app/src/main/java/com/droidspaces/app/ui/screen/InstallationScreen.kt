package com.droidspaces.app.ui.screen

import com.droidspaces.app.ui.component.PrimaryActionBottomBar
import androidx.compose.ui.graphics.Color

import androidx.activity.compose.BackHandler
import androidx.compose.animation.core.*
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowForward
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.droidspaces.app.util.InstallationStep
import com.droidspaces.app.util.ModuleInstallationStep
import com.droidspaces.app.R

import com.droidspaces.app.ui.viewmodel.AppStateViewModel

@Composable
fun InstallationScreen(
    appStateViewModel: AppStateViewModel,
    onInstallationComplete: () -> Unit
) {
    val context = LocalContext.current

    // Install orchestration + state live in AppStateViewModel (DT-6). Read as locals
    // so the UI below is unchanged; these are Compose state reads and recompose.
    val currentStep = appStateViewModel.installCurrentStep
    val currentModuleStep = appStateViewModel.installCurrentModuleStep
    val isSuccess = appStateViewModel.isInstallSuccess
    val errorMessage = appStateViewModel.installErrorMessage
    val isInstallingModule = appStateViewModel.isInstallingModule
    val rebootRecommended = appStateViewModel.installRebootRecommended

    // Completely block the back gesture in every state. This screen must be
    // left only via the Continue button, whose handler decides the next
    // destination and triggers the post-install refresh. A raw back-stack pop
    // would skip that and strand the user on a stale screen (e.g. the
    // "update available" card still showing after the update finished).
    BackHandler(enabled = true) {
        // Intentionally no-op while installing, on success and on error.
    }

    // Run the install orchestration (idempotent inside the ViewModel).
    LaunchedEffect(Unit) {
        appStateViewModel.performInstallation()
    }

    Scaffold(
        containerColor = Color.Transparent,
        bottomBar = {
            // Show the Continue button once the work is finished, whether it
            // succeeded or failed - it is the only accepted way off this screen.
            if (isSuccess || errorMessage != null) {
                PrimaryActionBottomBar(
                    label = context.getString(R.string.continue_button),
                    icon = if (isSuccess) Icons.Default.Check else Icons.AutoMirrored.Filled.ArrowForward,
                    onClick = onInstallationComplete
                )
            }
        }
    ) { innerPadding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
                .padding(horizontal = 24.dp)
                .padding(top = 24.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center
        ) {
            // Main icon with animation
            InstallationIcon(
                isSuccess = isSuccess,
                hasError = errorMessage != null
            )
    
            Spacer(modifier = Modifier.height(32.dp))
    
            // Title
            Text(
                text = when {
                    isSuccess -> context.getString(R.string.installation_complete)
                    errorMessage != null -> context.getString(R.string.installation_failed)
                    isInstallingModule -> context.getString(R.string.installing_module)
                    else -> context.getString(R.string.installing_droidspaces)
                },
                style = MaterialTheme.typography.headlineMedium,
                fontWeight = FontWeight.Bold,
                textAlign = TextAlign.Center
            )
    
            Spacer(modifier = Modifier.height(16.dp))
    
            // Status messages in a card (MMRL style)
            Surface(
                modifier = Modifier.fillMaxWidth(),
                color = MaterialTheme.colorScheme.surfaceContainerHigh,
                shape = RoundedCornerShape(20.dp),
                border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f))
            ) {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(20.dp),
                    horizontalAlignment = Alignment.CenterHorizontally
                ) {
                    when {
                        isSuccess -> {
                            Text(
                                text = if (isInstallingModule) {
                                    context.getString(R.string.module_installed_success)
                                } else {
                                    context.getString(R.string.backend_installed_success)
                                },
                                style = MaterialTheme.typography.bodyLarge,
                                color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f),
                                textAlign = TextAlign.Center
                            )
    
                        }
                        errorMessage != null -> {
                            Text(
                                text = errorMessage,
                                style = MaterialTheme.typography.bodyLarge,
                                color = MaterialTheme.colorScheme.error,
                                textAlign = TextAlign.Center
                            )
                        }
                        else -> {
                            // Show current step
                            if (isInstallingModule) {
                                when (currentModuleStep) {
                                    is ModuleInstallationStep.RemovingOldModule -> {
                                        StepText(context.getString(R.string.removing_old_module))
                                    }
                                    is ModuleInstallationStep.ExtractingAssets -> {
                                        StepText(context.getString(R.string.extracting_module_files))
                                    }
                                    is ModuleInstallationStep.CopyingModule -> {
                                        StepText(context.getString(R.string.installing_module_step))
                                    }
                                    is ModuleInstallationStep.SettingPermissions -> {
                                        StepText(context.getString(R.string.setting_permissions))
                                    }
                                    is ModuleInstallationStep.Verifying -> {
                                        StepText(context.getString(R.string.verifying_installation))
                                    }
                                    else -> {
                                        StepText(context.getString(R.string.preparing_module_installation))
                                    }
                                }
                            } else {
                                when (val step = currentStep) {
                                    is InstallationStep.DetectingArchitecture -> {
                                        StepText(context.getString(R.string.detected_architecture, step.arch))
                                    }
                                    is InstallationStep.CreatingDirectories -> {
                                        StepText(context.getString(R.string.creating_directories))
                                    }
                                    is InstallationStep.CopyingBinary -> {
                                        StepText(context.getString(R.string.installing_binary, step.binary))
                                    }
                                    is InstallationStep.SettingPermissions -> {
                                        StepText(context.getString(R.string.granting_permissions))
                                    }
                                    is InstallationStep.Verifying -> {
                                        StepText(context.getString(R.string.verifying_installation))
                                    }
                                    else -> {
                                        StepText(context.getString(R.string.preparing_installation))
                                    }
                                }
                            }
                        }
                    }
    
                    if (rebootRecommended) {
                        HorizontalDivider(
                            modifier = Modifier.padding(vertical = 12.dp),
                            color = MaterialTheme.colorScheme.primary.copy(alpha = 0.2f)
                        )
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.Center
                        ) {
                            Icon(
                                imageVector = Icons.Default.Info,
                                contentDescription = null,
                                tint = MaterialTheme.colorScheme.primary,
                                modifier = Modifier.size(20.dp)
                            )
                            Spacer(modifier = Modifier.width(8.dp))
                            Text(
                                text = context.getString(R.string.reboot_recommended),
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.primary,
                                fontWeight = FontWeight.Medium,
                                textAlign = TextAlign.Center
                            )
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun InstallationIcon(
    isSuccess: Boolean,
    hasError: Boolean
) {
    val infiniteTransition = rememberInfiniteTransition(label = "download_animation")

    when {
        isSuccess -> {
            // Success icon - checkmark
            Icon(
                imageVector = Icons.Default.CheckCircle,
                contentDescription = null,
                modifier = Modifier.size(80.dp),
                tint = MaterialTheme.colorScheme.primary
            )
        }
        hasError -> {
            // Error icon
            Icon(
                imageVector = Icons.Default.Error,
                contentDescription = null,
                modifier = Modifier.size(80.dp),
                tint = MaterialTheme.colorScheme.error
            )
        }
        else -> {
            // Download icon with pulsing animation
            val alpha by infiniteTransition.animateFloat(
                initialValue = 0.3f,
                targetValue = 1f,
                animationSpec = infiniteRepeatable(
                    animation = tween(1000, easing = FastOutSlowInEasing),
                    repeatMode = RepeatMode.Reverse
                ),
                label = "pulse_alpha"
            )

            Icon(
                imageVector = Icons.Default.Download,
                contentDescription = null,
                modifier = Modifier
                    .size(80.dp)
                    .alpha(alpha),
                tint = MaterialTheme.colorScheme.primary
            )
        }
    }
}

@Composable
private fun StepText(text: String) {
    Text(
        text = text,
        style = MaterialTheme.typography.bodyLarge,
        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f),
        textAlign = TextAlign.Center
    )
}

