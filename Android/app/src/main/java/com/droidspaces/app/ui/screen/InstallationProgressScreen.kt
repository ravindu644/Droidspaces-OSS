package com.droidspaces.app.ui.screen

import android.net.Uri
import android.util.Log
import android.widget.Toast
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material.icons.automirrored.filled.ArrowBack
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
import com.droidspaces.app.util.ViewModelLogger
import com.droidspaces.app.util.FilePickerUtils
import kotlinx.coroutines.launch
import com.droidspaces.app.ui.component.DialogFooterRow
import com.droidspaces.app.util.AnsiColorParser

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
        }
    }

    // Handle back button behavior
    BackHandler(enabled = true) {
        when (installationState) {
            InstallationState.ERROR -> onError()
            InstallationState.SUCCESS -> onSuccess()
            InstallationState.INSTALLING -> {
                // Blocked: Do nothing during installation
            }
        }
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
                        },
                        style = MaterialTheme.typography.titleLarge,
                        fontWeight = FontWeight.Bold
                    )
                },
                navigationIcon = {
                    if (installationState != InstallationState.INSTALLING) {
                        IconButton(onClick = {
                            if (installationState == InstallationState.SUCCESS) onSuccess() else onError()
                        }) {
                            Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = context.getString(R.string.back))
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
        ) {
            // The console grows with its lines up to whatever the buttons below leave it.
            TerminalConsole(
                logs = logs,
                isProcessing = installationState == InstallationState.INSTALLING,
                modifier = Modifier.fillMaxWidth().weight(1f, fill = false)
            )

            when (installationState) {
                InstallationState.SUCCESS -> {
                    Spacer(modifier = Modifier.height(12.dp))
                    Button(
                        onClick = onSuccess,
                        modifier = Modifier.fillMaxWidth().height(56.dp),
                        shape = RoundedCornerShape(16.dp),
                    ) {
                        Icon(Icons.Default.CheckCircle, contentDescription = null)
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(context.getString(R.string.done), style = MaterialTheme.typography.labelLarge)
                    }
                }
                InstallationState.ERROR -> {
                    Spacer(modifier = Modifier.height(12.dp))
                    DialogFooterRow(
                        dismissLabel = context.getString(R.string.close),
                        confirmLabel = context.getString(R.string.copy_logs),
                        onDismiss = onError,
                        onConfirm = {
                            val clipboard = context.getSystemService(android.content.Context.CLIPBOARD_SERVICE) as android.content.ClipboardManager
                            clipboard.setPrimaryClip(android.content.ClipData.newPlainText(context.getString(R.string.installation_logs), logs.joinToString("\n") { AnsiColorParser.stripAnsi(it.second) }))
                            Toast.makeText(context, R.string.logs_copied, Toast.LENGTH_SHORT).show()
                        }
                    )
                }
                InstallationState.INSTALLING -> {}
            }
        }
    }
}

