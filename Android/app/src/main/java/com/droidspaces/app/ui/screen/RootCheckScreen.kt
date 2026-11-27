package com.droidspaces.app.ui.screen

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.height
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Security
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.droidspaces.app.R
import com.droidspaces.app.ui.component.SetupActionBar
import com.droidspaces.app.ui.component.SetupBody
import com.droidspaces.app.ui.component.SetupFrame
import com.droidspaces.app.ui.component.SetupHero
import com.droidspaces.app.ui.component.SetupPage
import com.droidspaces.app.util.RootChecker
import com.droidspaces.app.util.RootStatus
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

@Composable
fun RootCheckScreen(
    rootStatus: RootStatus? = null,
    onRootCheck: ((RootStatus) -> Unit)? = null,
    onNavigateToInstallation: () -> Unit,
    onSkip: () -> Unit = {}
) {
    val scope = rememberCoroutineScope()
    var currentRootStatus by remember { mutableStateOf<RootStatus?>(rootStatus) }
    var isChecking by remember { mutableStateOf(false) }
    var hasCheckedRoot by remember { mutableStateOf(false) }

    fun checkRoot() {
        if (isChecking) return
        isChecking = true
        currentRootStatus = RootStatus.Checking
        hasCheckedRoot = true
        scope.launch {
            val startedAt = System.currentTimeMillis()
            val result = RootChecker.checkRootAccess()
            // A grant returns within a frame. Hold the loader for a beat so the hand-off
            // to the installer reads as one loader, not a flash.
            delay((600 - (System.currentTimeMillis() - startedAt)).coerceAtLeast(0))
            onRootCheck?.invoke(result)
            // A grant is the only thing this screen waited for, so it carries straight on
            // with the loader still up and the installer's own loader takes over. Only a
            // denial is shown here.
            if (result == RootStatus.Granted) {
                onNavigateToInstallation()
            } else {
                currentRootStatus = result
                isChecking = false
            }
        }
    }

    val checking = isChecking || currentRootStatus == RootStatus.Checking
    val denied = currentRootStatus == RootStatus.Denied
    val scheme = MaterialTheme.colorScheme
    val motion = MaterialTheme.motionScheme

    Scaffold(
        containerColor = Color.Transparent,
        bottomBar = {
            // The title already says a check is running, so the button fades out for the
            // duration rather than saying it again. It keeps its space so the hero stays put.
            val barAlpha by animateFloatAsState(
                targetValue = if (checking) 0f else 1f,
                animationSpec = motion.fastEffectsSpec(),
                label = "check_alpha"
            )
            SetupActionBar(
                label = stringResource(R.string.check_root_access),
                icon = Icons.Default.Security,
                onClick = ::checkRoot,
                modifier = Modifier.alpha(barAlpha),
                enabled = !checking
            )
        }
    ) { innerPadding ->
        SetupPage(innerPadding = innerPadding) {
            SetupFrame(
                title = stringResource(
                    when {
                        checking -> R.string.checking_root
                        denied -> R.string.root_denied
                        else -> R.string.root_access_title
                    }
                ),
                hero = {
                    SetupHero(
                        loading = checking,
                        glyph = if (denied) Icons.Default.Close else Icons.Default.Security,
                        container = if (denied) scheme.errorContainer else scheme.primaryContainer,
                        onContainer = if (denied) scheme.onErrorContainer else scheme.onPrimaryContainer
                    )
                }
            ) {
                SetupBody(stringResource(if (denied) R.string.root_required_message else R.string.root_access_body))
                // Skip lives under the copy rather than in the bar, so the bar is the same
                // height as every other setup screen's and the hero sits on the same row.
                val showSkip = denied && hasCheckedRoot
                val skipAlpha by animateFloatAsState(
                    targetValue = if (showSkip) 1f else 0f,
                    animationSpec = motion.fastEffectsSpec(),
                    label = "skip_alpha"
                )
                Spacer(modifier = Modifier.height(12.dp))
                TextButton(onClick = onSkip, enabled = showSkip, modifier = Modifier.alpha(skipAlpha)) {
                    Text(text = stringResource(R.string.skip), fontWeight = FontWeight.SemiBold)
                }
            }
        }
    }
}
