package com.droidspaces.app.ui.screen

import com.droidspaces.app.ui.component.PrimaryActionBottomBar

import androidx.compose.animation.animateContentSize
import androidx.compose.animation.core.animateFloatAsState
import kotlinx.coroutines.delay
import com.droidspaces.app.util.AnimationUtils
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.droidspaces.app.R
import com.droidspaces.app.util.RootChecker
import com.droidspaces.app.util.RootStatus
import com.droidspaces.app.ui.util.LoadingIndicator
import com.droidspaces.app.ui.util.LoadingSize
import kotlinx.coroutines.launch

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
    var hasCheckedRoot by remember { mutableStateOf(false) }
    var titleVisible by remember { mutableStateOf(false) }
    var cardVisible by remember { mutableStateOf(false) }

    LaunchedEffect(Unit) {
        delay(60); titleVisible = true
        delay(120); cardVisible = true
    }

    val titleAlpha by animateFloatAsState(if (titleVisible) 1f else 0f, AnimationUtils.fadeInSpec(), label = "titleAlpha")
    val cardAlpha by animateFloatAsState(if (cardVisible) 1f else 0f, AnimationUtils.fadeInSpec(), label = "cardAlpha")

    fun checkRoot() {
        if (isChecking) return
        isChecking = true
        currentRootStatus = RootStatus.Checking
        hasCheckedRoot = true
        scope.launch {
            val result = RootChecker.checkRootAccess()
            currentRootStatus = result
            onRootCheck?.invoke(result)
            isChecking = false
        }
    }

    val btnShape = RoundedCornerShape(20.dp)

    Scaffold(
        containerColor = Color.Transparent,
        bottomBar = {
            PrimaryActionBottomBar(
                label = context.getString(
                    if (currentRootStatus == RootStatus.Granted) R.string.continue_button
                    else if (isChecking) R.string.checking_root
                    else R.string.check_root_access
                ),
                icon = if (currentRootStatus == RootStatus.Granted) Icons.Default.CheckCircle else Icons.Default.Shield,
                onClick = {
                    if (currentRootStatus == RootStatus.Granted) onNavigateToInstallation()
                    else checkRoot()
                },
                enabled = !isChecking && currentRootStatus != RootStatus.Checking,
                disabledContainerColor = MaterialTheme.colorScheme.primary,
                disabledContentColor = MaterialTheme.colorScheme.onPrimary,
                horizontalPadding = 20.dp,
                labelFontSize = 16.sp,
                secondaryAction = {
                    androidx.compose.animation.AnimatedVisibility(
                        visible = currentRootStatus == RootStatus.Denied && hasCheckedRoot,
                        enter = androidx.compose.animation.expandVertically() + androidx.compose.animation.fadeIn(),
                        exit = androidx.compose.animation.shrinkVertically() + androidx.compose.animation.fadeOut()
                    ) {
                        Surface(
                            modifier = Modifier
                                .fillMaxWidth()
                                .clip(btnShape)
                                .clickable(onClick = onSkip),
                            shape = btnShape,
                            color = Color.Transparent,
                            border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.4f)),
                            tonalElevation = 0.dp
                        ) {
                            Box(modifier = Modifier.padding(vertical = 16.dp), contentAlignment = Alignment.Center) {
                                Text(
                                    text = context.getString(R.string.skip),
                                    style = MaterialTheme.typography.labelLarge,
                                    fontWeight = FontWeight.SemiBold,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }
                        }
                    }
                }
            )
        }
    ) { innerPadding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
                .padding(24.dp)
                .verticalScroll(rememberScrollState()),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(32.dp, Alignment.CenterVertically)
        ) {
            // Title
            Text(
                text = context.getString(R.string.root_check_title),
                style = MaterialTheme.typography.headlineMedium,
                fontWeight = FontWeight.Bold,
                textAlign = TextAlign.Center,
                modifier = Modifier.alpha(titleAlpha)
            )

            // Status Card
            val cardAccentColor = when (currentRootStatus) {
                RootStatus.Granted -> MaterialTheme.colorScheme.primary
                RootStatus.Denied -> MaterialTheme.colorScheme.error
                else -> MaterialTheme.colorScheme.onSurfaceVariant
            }

            Surface(
                modifier = Modifier.fillMaxWidth().alpha(cardAlpha),
                shape = RoundedCornerShape(20.dp),
                color = MaterialTheme.colorScheme.surfaceContainer,
                border = BorderStroke(1.dp, cardAccentColor.copy(alpha = 0.3f))
            ) {
                Column(
                    modifier = Modifier.fillMaxWidth().padding(24.dp),
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.spacedBy(16.dp)
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
                            Surface(
                                shape = RoundedCornerShape(12.dp),
                                color = MaterialTheme.colorScheme.primary.copy(alpha = 0.1f),
                                tonalElevation = 0.dp
                            ) {
                                Icon(
                                    imageVector = Icons.Default.CheckCircle,
                                    contentDescription = null,
                                    modifier = Modifier.padding(12.dp).size(32.dp),
                                    tint = MaterialTheme.colorScheme.primary
                                )
                            }
                            Text(
                                text = context.getString(R.string.root_granted),
                                style = MaterialTheme.typography.titleMedium,
                                fontWeight = FontWeight.Bold,
                                color = MaterialTheme.colorScheme.primary,
                                textAlign = TextAlign.Center
                            )
                            Text(
                                text = context.getString(R.string.root_available_message),
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                textAlign = TextAlign.Center
                            )
                        }
                        currentRootStatus == RootStatus.Denied -> {
                            Surface(
                                shape = RoundedCornerShape(12.dp),
                                color = MaterialTheme.colorScheme.error.copy(alpha = 0.1f),
                                tonalElevation = 0.dp
                            ) {
                                Icon(
                                    imageVector = Icons.Default.Error,
                                    contentDescription = null,
                                    modifier = Modifier.padding(12.dp).size(32.dp),
                                    tint = MaterialTheme.colorScheme.error
                                )
                            }
                            Text(
                                text = context.getString(R.string.root_denied),
                                style = MaterialTheme.typography.titleMedium,
                                fontWeight = FontWeight.Bold,
                                color = MaterialTheme.colorScheme.error,
                                textAlign = TextAlign.Center
                            )
                            Text(
                                text = context.getString(R.string.root_required_message),
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                textAlign = TextAlign.Center
                            )
                        }
                        else -> {
                            Surface(
                                shape = RoundedCornerShape(12.dp),
                                color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.1f),
                                tonalElevation = 0.dp
                            ) {
                                Icon(
                                    imageVector = Icons.Default.Security,
                                    contentDescription = null,
                                    modifier = Modifier.padding(12.dp).size(32.dp),
                                    tint = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }
                            Text(
                                text = context.getString(R.string.check_root_access_click),
                                style = MaterialTheme.typography.bodyMedium,
                                textAlign = TextAlign.Center
                            )
                        }
                    }
                }
            }
        }
    }
}
