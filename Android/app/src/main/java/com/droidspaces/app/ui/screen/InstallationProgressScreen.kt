package com.droidspaces.app.ui.screen

import android.net.Uri
import android.util.Log
import android.widget.Toast
import androidx.activity.compose.BackHandler
import androidx.compose.animation.animateContentSize
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.spring
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.droidspaces.app.R
import com.droidspaces.app.ui.component.TerminalConsole
import com.droidspaces.app.util.ContainerInfo
import com.droidspaces.app.util.ContainerInstaller
import com.droidspaces.app.util.ContainerLogger
import com.droidspaces.app.util.FilePickerUtils
import com.droidspaces.app.util.ViewModelLogger
import kotlinx.coroutines.launch

enum class InstallationState {
    INSTALLING,
    SUCCESS,
    ERROR
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun InstallationProgressScreen(
    tarballUri: Uri,
    config: ContainerInfo,
    onSuccess: () -> Unit,
    onError: () -> Unit
) {
    val context = LocalContext.current

    var installationState by remember { mutableStateOf(InstallationState.INSTALLING) }
    val logs = remember { mutableStateListOf<Pair<Int, String>>() }

    val logger = remember {
        ViewModelLogger { level, message ->
            // Direct add to SnapshotStateList - already on Main thread
            logs.add(level to message)
        }.apply {
            verbose = true
        }
    }

    // Prevent back button during installation
    BackHandler(enabled = installationState == InstallationState.INSTALLING) {
        // Block back button during installation
    }

    // Start installation
    LaunchedEffect(Unit) {
        if (logs.isEmpty()) {
            logger.i("Starting container installation...")
            logger.i("Container: ${config.name}")
            logger.i("Hostname: ${config.hostname}")
            val tarballName = FilePickerUtils.getFileName(context, tarballUri) ?: tarballUri.toString()
            logger.i("Tarball: $tarballName")

            val result = ContainerInstaller.installContainer(
                context = context,
                tarballUri = tarballUri,
                config = config,
                logger = logger
            )

            installationState = if (result.isSuccess) {
                logger.i("Installation completed successfully!")
                InstallationState.SUCCESS
            } else {
                logger.e("Installation failed: ${result.exceptionOrNull()?.message}")
                InstallationState.ERROR
            }
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Text(
                        when (installationState) {
                            InstallationState.INSTALLING -> context.getString(R.string.installing_container)
                            InstallationState.SUCCESS -> context.getString(R.string.installation_complete)
                            InstallationState.ERROR -> context.getString(R.string.installation_failed)
                        }
                    )
                }
            )
        }
    ) { innerPadding ->
        BoxWithConstraints(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
                .padding(horizontal = 24.dp, vertical = 24.dp)
        ) {
            val consoleMaxHeight = if (installationState == InstallationState.INSTALLING) {
                maxHeight
            } else {
                maxHeight - ButtonDefaults.MinHeight - 12.dp
            }

            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .animateContentSize(spring(stiffness = Spring.StiffnessLow))
            ) {
                // Terminal Console
                TerminalConsole(
                    logs = logs,
                    isProcessing = installationState == InstallationState.INSTALLING,
                    modifier = Modifier.fillMaxWidth(),
                    maxHeight = consoleMaxHeight
                )

                // Action Buttons
                when (installationState) {
                    InstallationState.SUCCESS -> {
                        Spacer(modifier = Modifier.height(12.dp))
                        Button(
                            onClick = onSuccess,
                            modifier = Modifier
                                .fillMaxWidth()
                                .height(56.dp),
                            colors = ButtonDefaults.buttonColors(
                                containerColor = MaterialTheme.colorScheme.primary
                            )
                        ) {
                            Icon(Icons.Default.CheckCircle, contentDescription = null)
                            Spacer(modifier = Modifier.width(8.dp))
                            Text(context.getString(R.string.done), style = MaterialTheme.typography.labelLarge)
                        }
                    }
                    InstallationState.ERROR -> {
                        Spacer(modifier = Modifier.height(12.dp))
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.spacedBy(12.dp)
                        ) {
                            OutlinedButton(
                                onClick = onError,
                                modifier = Modifier
                                    .weight(1f)
                                    .height(56.dp)
                            ) {
                                Text(context.getString(R.string.close), style = MaterialTheme.typography.labelLarge)
                            }
                            Button(
                                onClick = {
                                    // Copy logs to clipboard
                                    val logText = logs.joinToString("\n") { it.second }
                                    val clipboard = context.getSystemService(android.content.Context.CLIPBOARD_SERVICE) as android.content.ClipboardManager
                                    val clip = android.content.ClipData.newPlainText(context.getString(R.string.installation_logs), logText)
                                    clipboard.setPrimaryClip(clip)
                                    Toast.makeText(context, R.string.logs_copied, Toast.LENGTH_SHORT).show()
                                },
                                modifier = Modifier
                                    .weight(1f)
                                    .height(56.dp)
                            ) {
                                Icon(Icons.Default.ContentCopy, contentDescription = null)
                                Spacer(modifier = Modifier.width(8.dp))
                                Text(context.getString(R.string.copy_logs), style = MaterialTheme.typography.labelLarge)
                            }
                        }
                    }
                    InstallationState.INSTALLING -> {
                        // No buttons during installation
                    }
                }
            }
        }
    }
}

