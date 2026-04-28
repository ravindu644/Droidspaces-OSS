package com.droidspaces.app.ui.screen

import androidx.compose.animation.core.animateFloatAsState
import kotlinx.coroutines.delay
import com.droidspaces.app.util.AnimationUtils
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.droidspaces.app.util.RootChecker
import com.droidspaces.app.util.RootStatus
import com.droidspaces.app.ui.util.LoadingIndicator
import com.droidspaces.app.ui.util.LoadingSize
import kotlinx.coroutines.launch
import androidx.compose.ui.platform.LocalContext
import com.droidspaces.app.R

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun RootCheckScreen(
    rootStatus: RootStatus? = null,
    onRootCheck: ((RootStatus) -> Unit)? = null,
    onNavigateToInstallation: () -> Unit,
    onSkip: () -> Unit = {}
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var currentRootStatus by remember { mutableStateOf<RootStatus?>(rootStatus) }
    var isChecking by remember { mutableStateOf(false) }
    var hasCheckedRoot by remember { mutableStateOf(false) } // Track if user has clicked check root
    var skipButtonVisible by remember { mutableStateOf(false) } // Track skip button visibility

    // Premium staggered fade-in animations with spring physics - elegant and smooth
    // Artificial delays make the animation sequence feel natural and polished
    var titleVisible by remember { mutableStateOf(false) }
    var cardVisible by remember { mutableStateOf(false) }
    var buttonVisible by remember { mutableStateOf(false) }

    LaunchedEffect(Unit) {
        delay(50) // Initial pause for elegant entry
        titleVisible = true
        delay(100) // Pause between elements
        cardVisible = true
        delay(100) // Smooth transition to button
        buttonVisible = true
    }

    val titleAlpha by animateFloatAsState(
        targetValue = if (titleVisible) 1f else 0f,
        animationSpec = AnimationUtils.fadeInSpec(),
        label = "titleAlpha"
    )
    val cardAlpha by animateFloatAsState(
        targetValue = if (cardVisible) 1f else 0f,
        animationSpec = AnimationUtils.fadeInSpec(),
        label = "cardAlpha"
    )
    val buttonAlpha by animateFloatAsState(
        targetValue = if (buttonVisible) 1f else 0f,
        animationSpec = AnimationUtils.fadeInSpec(),
        label = "buttonAlpha"
    )

    // Skip button fade-in animation
    val skipButtonAlpha by animateFloatAsState(
        targetValue = if (skipButtonVisible) 1f else 0f,
        animationSpec = AnimationUtils.slowSpec(),
        label = "skipButtonAlpha"
    )

    fun checkRoot() {
        if (isChecking) return
        isChecking = true
        currentRootStatus = RootStatus.Checking
        hasCheckedRoot = true // Mark that user has checked root
        scope.launch {
                val result = RootChecker.checkRootAccess()
                currentRootStatus = result
                onRootCheck?.invoke(result)
                isChecking = false
                // Show skip button with animation after check completes
                delay(100) // Small delay for smooth animation
                skipButtonVisible = true
        }
    }

    Scaffold { innerPadding ->
        Box(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
                .padding(24.dp),
            contentAlignment = Alignment.Center
            ) {
                val scrollState = rememberScrollState()
                Column(
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.spacedBy(24.dp, Alignment.CenterVertically),
                    modifier = Modifier
                        .fillMaxSize()
                        .verticalScroll(scrollState)
                ) {
                    // Title - fades in first
                    Text(
                        text = context.getString(R.string.root_check_title),
                        style = MaterialTheme.typography.headlineMedium,
                        fontWeight = FontWeight.Bold,
                        textAlign = TextAlign.Center,
                        modifier = Modifier.alpha(titleAlpha)
                    )

                    // Root Status Card - fades in second
                    Surface(
                        modifier = Modifier
                            .fillMaxWidth()
                            .alpha(cardAlpha),
                        shape = RoundedCornerShape(20.dp),
                        color = when (currentRootStatus) {
                            RootStatus.Granted -> MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.6f)
                            RootStatus.Denied -> MaterialTheme.colorScheme.errorContainer.copy(alpha = 0.6f)
                            else -> MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f)
                        },
                        border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f))
                    ) {
                        Column(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(24.dp),
                            horizontalAlignment = Alignment.CenterHorizontally,
                            verticalArrangement = Arrangement.spacedBy(12.dp)
                        ) {
                            when {
                                isChecking || currentRootStatus == RootStatus.Checking -> {
                                    LoadingIndicator(size = LoadingSize.Large)
                                    Text(
                                        text = context.getString(R.string.checking_root),
                                        style = MaterialTheme.typography.bodyMedium,
                                        textAlign = TextAlign.Center
                                    )
                                }
                                currentRootStatus == RootStatus.Granted -> {
                                    Icon(
                                        imageVector = Icons.Default.CheckCircle,
                                        contentDescription = null,
                                        modifier = Modifier.size(48.dp),
                                        tint = MaterialTheme.colorScheme.primary
                                    )
                                    Text(
                                        text = context.getString(R.string.root_granted),
                                        style = MaterialTheme.typography.titleMedium,
                                        fontWeight = FontWeight.SemiBold,
                                        textAlign = TextAlign.Center
                                    )
                                    Text(
                                        text = context.getString(R.string.root_available_message),
                                        style = MaterialTheme.typography.bodySmall,
                                        color = MaterialTheme.colorScheme.onPrimaryContainer,
                                        textAlign = TextAlign.Center
                                    )
                                }
                                currentRootStatus == RootStatus.Denied -> {
                                    Icon(
                                        imageVector = Icons.Default.Error,
                                        contentDescription = null,
                                        modifier = Modifier.size(48.dp),
                                        tint = MaterialTheme.colorScheme.error
                                    )
                                    Text(
                                        text = context.getString(R.string.root_denied),
                                        style = MaterialTheme.typography.titleMedium,
                                        color = MaterialTheme.colorScheme.onErrorContainer,
                                        textAlign = TextAlign.Center
                                    )
                                    Text(
                                        text = context.getString(R.string.root_required_message),
                                        style = MaterialTheme.typography.bodySmall,
                                        color = MaterialTheme.colorScheme.onErrorContainer,
                                        textAlign = TextAlign.Center
                                    )
                                }
                                else -> {
                                    Icon(
                                        imageVector = Icons.Default.Security,
                                        contentDescription = null,
                                        modifier = Modifier.size(48.dp),
                                        tint = MaterialTheme.colorScheme.onSurfaceVariant
                                    )
                                    Text(
                                        text = context.getString(R.string.check_root_access_click),
                                        style = MaterialTheme.typography.bodyMedium,
                                        textAlign = TextAlign.Center
                                    )
                                }
                            }
                        }
                    }

                // Button - fades in last with charming animation
                if (currentRootStatus == RootStatus.Granted) {
                    Button(
                        onClick = onNavigateToInstallation,
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(56.dp)
                            .alpha(buttonAlpha),
                ) {
                    Text(
                        text = context.getString(R.string.continue_button),
                        style = MaterialTheme.typography.labelLarge
                    )
                }
                } else {
                    Column(
                        modifier = Modifier.fillMaxWidth(),
                        verticalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        Button(
                            onClick = { checkRoot() },
                            enabled = !isChecking && currentRootStatus != RootStatus.Checking,
                            modifier = Modifier
                                .fillMaxWidth()
                                .height(56.dp)
                                .alpha(buttonAlpha),
                            shape = RoundedCornerShape(20.dp)
                        ) {
                            Text(
                                text = if (isChecking) context.getString(R.string.checking_root) else context.getString(R.string.check_root_access),
                                style = MaterialTheme.typography.labelLarge
                            )
                        }

                        // Skip button - only shown after user clicks "Check Root Access"
                        if (hasCheckedRoot) {
                            TextButton(
                                onClick = onSkip,
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .alpha(skipButtonAlpha)
                            ) {
                                Text(
                                    text = context.getString(R.string.skip),
                                    style = MaterialTheme.typography.labelLarge
                                )
                            }
                        }
                    }
                }
                }
        }
    }
}
