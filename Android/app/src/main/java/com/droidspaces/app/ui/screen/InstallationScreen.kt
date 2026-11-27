package com.droidspaces.app.ui.screen

import androidx.activity.compose.BackHandler
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowForward
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.droidspaces.app.R
import com.droidspaces.app.ui.component.SetupActionBar
import com.droidspaces.app.ui.component.SetupBody
import com.droidspaces.app.ui.component.SetupFrame
import com.droidspaces.app.ui.component.SetupHero
import com.droidspaces.app.ui.component.SetupPage
import com.droidspaces.app.ui.theme.onWarningContainer
import com.droidspaces.app.ui.theme.warningContainer
import com.droidspaces.app.ui.viewmodel.AppStateViewModel
import com.droidspaces.app.util.HostCapabilities

private enum class InstallPhase { Installing, Success, Warning, Failed }

@Composable
fun InstallationScreen(
    appStateViewModel: AppStateViewModel,
    onInstallationComplete: () -> Unit
) {
    val context = LocalContext.current

    // Install orchestration and state live in AppStateViewModel, so they
    // survive rotation; these are Compose state reads and recompose.
    val isSuccess = appStateViewModel.isInstallSuccess
    val errorMessage = appStateViewModel.installErrorMessage
    val rebootRecommended = appStateViewModel.installRebootRecommended
    val kernelUnsupported = HostCapabilities.state.collectAsState().value?.requirementsMet == false

    // The ViewModel outlives this screen and performInstallation() resets the
    // previous outcome only once the effect below runs, which is after the
    // first frame. Until then, show the loader rather than the stale result.
    var started by rememberSaveable { mutableStateOf(false) }
    val phase = when {
        !started -> InstallPhase.Installing
        isSuccess && kernelUnsupported -> InstallPhase.Warning
        isSuccess -> InstallPhase.Success
        errorMessage != null -> InstallPhase.Failed
        else -> InstallPhase.Installing
    }
    val done = phase != InstallPhase.Installing
    val motion = MaterialTheme.motionScheme

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
        started = true
        appStateViewModel.performInstallation()
    }

    Scaffold(
        containerColor = Color.Transparent,
        bottomBar = {
            // Continue is the only accepted way off this screen, so it is disabled and
            // invisible until the work is finished, whether it succeeded or failed. It
            // keeps its space the whole time so the content above does not jump when it
            // appears.
            val barAlpha by animateFloatAsState(
                targetValue = if (done) 1f else 0f,
                animationSpec = motion.fastEffectsSpec(),
                label = "continue_alpha"
            )
            SetupActionBar(
                label = context.getString(R.string.continue_button),
                icon = if (isSuccess) Icons.Default.Check else Icons.AutoMirrored.Filled.ArrowForward,
                onClick = onInstallationComplete,
                modifier = Modifier.alpha(barAlpha),
                enabled = done
            )
        }
    ) { innerPadding ->
        val scheme = MaterialTheme.colorScheme
        // One switch for the whole hero, so a new phase cannot get a glyph on the wrong tint.
        val (glyph, container, onContainer) = when (phase) {
            InstallPhase.Warning -> Triple(Icons.Default.Warning, scheme.warningContainer, scheme.onWarningContainer)
            InstallPhase.Failed -> Triple(Icons.Default.Close, scheme.errorContainer, scheme.onErrorContainer)
            else -> Triple(Icons.Default.Check, scheme.primaryContainer, scheme.onPrimaryContainer)
        }
        SetupPage(innerPadding = innerPadding) {
            SetupFrame(
                title = context.getString(
                    when (phase) {
                        InstallPhase.Installing -> R.string.installing_droidspaces
                        InstallPhase.Failed -> R.string.installation_failed
                        else -> R.string.installation_complete
                    }
                ),
                hero = {
                    SetupHero(
                        loading = phase == InstallPhase.Installing,
                        glyph = glyph,
                        container = container,
                        onContainer = onContainer
                    )
                }
            ) {
                SetupBody(
                    text = when (phase) {
                        InstallPhase.Installing -> context.getString(R.string.installing_backend_message)
                        InstallPhase.Warning -> context.getString(R.string.backend_installed_unsupported_kernel)
                        InstallPhase.Failed -> errorMessage.orEmpty()
                        else -> context.getString(R.string.backend_installed_success)
                    },
                    color = when (phase) {
                        InstallPhase.Failed -> scheme.error
                        InstallPhase.Warning -> scheme.onWarningContainer
                        else -> scheme.onSurfaceVariant
                    }
                )

                AnimatedVisibility(
                    visible = phase == InstallPhase.Success && rebootRecommended,
                    enter = fadeIn(motion.fastEffectsSpec()),
                    exit = fadeOut(motion.fastEffectsSpec())
                ) {
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        Spacer(modifier = Modifier.height(24.dp))
                        Surface(
                            shape = RoundedCornerShape(20.dp),
                            color = MaterialTheme.colorScheme.surfaceContainer,
                            tonalElevation = 0.dp
                        ) {
                            Row(
                                modifier = Modifier.padding(16.dp),
                                verticalAlignment = Alignment.Top,
                                horizontalArrangement = Arrangement.spacedBy(12.dp)
                            ) {
                                Icon(
                                    imageVector = Icons.Default.Info,
                                    contentDescription = null,
                                    tint = MaterialTheme.colorScheme.primary,
                                    modifier = Modifier.size(20.dp)
                                )
                                Text(
                                    text = context.getString(R.string.reboot_recommended),
                                    style = MaterialTheme.typography.bodyMedium
                                )
                            }
                        }
                    }
                }
            }
        }
    }
}
