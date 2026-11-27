package com.droidspaces.app.ui.screen

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.ArrowForward
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import com.droidspaces.app.R
import com.droidspaces.app.ui.component.PrimaryActionBottomBar
import com.droidspaces.app.ui.component.SetupBody
import com.droidspaces.app.ui.component.SetupFrame
import com.droidspaces.app.ui.component.SetupHero
import com.droidspaces.app.ui.component.SetupPage
import com.droidspaces.app.ui.theme.onWarningContainer
import com.droidspaces.app.ui.theme.warningContainer

/**
 * Shown between the name and the configuration page when the archive carried its own
 * container.config. Everything in it is applied as is, hardware access and privileged
 * mode included, so this page is the one gate: it sends the user into the form to read it.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun RecommendedConfigScreen(onNext: () -> Unit, onBack: () -> Unit) {
    val scheme = MaterialTheme.colorScheme
    Scaffold(
        containerColor = Color.Transparent,
        topBar = {
            TopAppBar(
                title = { Text(stringResource(R.string.container_setup), style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold) },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = stringResource(R.string.back))
                    }
                }
            )
        },
        bottomBar = {
            PrimaryActionBottomBar(
                label = stringResource(R.string.rootfs_config_review),
                icon = Icons.AutoMirrored.Filled.ArrowForward,
                onClick = onNext
            )
        }
    ) { innerPadding ->
        SetupPage(innerPadding = innerPadding) {
            SetupFrame(
                title = stringResource(R.string.rootfs_config_loaded_title),
                hero = {
                    SetupHero(
                        loading = false,
                        glyph = Icons.Default.Warning,
                        container = scheme.warningContainer,
                        onContainer = scheme.onWarningContainer
                    )
                }
            ) {
                SetupBody(stringResource(R.string.rootfs_config_loaded))
            }
        }
    }
}
