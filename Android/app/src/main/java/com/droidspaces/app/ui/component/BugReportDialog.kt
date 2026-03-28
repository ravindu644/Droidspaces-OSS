package com.droidspaces.app.ui.component

import androidx.compose.runtime.*
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import com.droidspaces.app.R
import com.droidspaces.app.util.ContainerOperationExecutor
import com.droidspaces.app.util.ViewModelLogger
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File

@Composable
fun BugReportDialog(
    onDismiss: () -> Unit
) {
    val context = LocalContext.current
    val logs = remember { mutableStateListOf<Pair<Int, String>>() }
    var isProcessing by remember { mutableStateOf(true) }

    val logger = remember {
        ViewModelLogger { level, message ->
            logs.add(level to message)
        }.apply {
            verbose = true
        }
    }

    // Run bugreport.sh
    LaunchedEffect(Unit) {
        isProcessing = true
        try {
            // Step 1: Extract bugreport.sh to cache
            val tempFile = File(context.cacheDir, "bugreport.sh")
            withContext(Dispatchers.IO) {
                context.assets.open("bugreport.sh").use { input ->
                    tempFile.outputStream().use { output ->
                        input.copyTo(output)
                    }
                }
            }

            // Step 2: Run it with root
            // We use ContainerOperationExecutor to get real-time logs in the terminal
            val command = "sh ${tempFile.absolutePath}"
            ContainerOperationExecutor.executeCommand(
                command = command,
                operation = "bug report",
                logger = logger,
                skipHeader = false,
                operationCompletedMessage = "Bug report generation completed!"
            )
            
            // Clean up
            tempFile.delete()
        } catch (e: Exception) {
            logger.e("Failed to generate bug report: ${e.message}")
        } finally {
            isProcessing = false
        }
    }

    TerminalDialog(
        title = stringResource(R.string.bug_report_title),
        logs = logs,
        onDismiss = onDismiss,
        isBlocking = isProcessing
    )
}
