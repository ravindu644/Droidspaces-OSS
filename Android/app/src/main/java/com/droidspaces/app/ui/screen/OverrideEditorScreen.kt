package com.droidspaces.app.ui.screen

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Save
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.unit.dp
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import com.droidspaces.app.ui.component.DsSnackbarHost
import com.droidspaces.app.ui.theme.JetBrainsMono
import com.droidspaces.app.R
import com.droidspaces.app.ui.util.FullScreenLoading
import com.droidspaces.app.ui.util.ProgressDialog
import com.droidspaces.app.ui.util.showError
import com.droidspaces.app.ui.util.showSuccess
import com.droidspaces.app.util.ContainerSystemdManager
import kotlinx.coroutines.launch

private const val OVERRIDE_TEMPLATE = "[Service]\n"

/**
 * Editor for a unit's override.conf drop-in (`/etc/systemd/system/<unit>.d/override.conf`),
 * equivalent to `systemctl edit <unit>` from the CLI. Reached from the
 * "Edit override" overflow-menu item on [SystemdScreen], or the edit icon on
 * [UnitDetailScreen].
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun OverrideEditorScreen(
    containerName: String,
    unitName: String,
    onNavigateBack: () -> Unit,
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val snackbarHostState = remember { SnackbarHostState() }

    var isLoading by remember { mutableStateOf(true) }
    var isSaving by remember { mutableStateOf(false) }
    var hasExistingOverride by remember { mutableStateOf(false) }
    var text by remember { mutableStateOf("") }

    LaunchedEffect(containerName, unitName) {
        val existing = ContainerSystemdManager.getOverrideConf(containerName, unitName)
        hasExistingOverride = existing != null
        text = existing ?: OVERRIDE_TEMPLATE
        isLoading = false
    }

    fun save() {
        isSaving = true
        scope.launch {
            val result = ContainerSystemdManager.setOverrideConf(containerName, unitName, text)
            isSaving = false
            if (result.isSuccess) {
                hasExistingOverride = true
                snackbarHostState.showSuccess(context.getString(R.string.override_saved))
            } else {
                val message = (result.output + result.error).firstOrNull() ?: context.getString(R.string.failed_to_save_override)
                snackbarHostState.showError(message)
            }
        }
    }

    fun delete() {
        isSaving = true
        scope.launch {
            val result = ContainerSystemdManager.deleteOverrideConf(containerName, unitName)
            isSaving = false
            if (result.isSuccess) {
                text = OVERRIDE_TEMPLATE
                hasExistingOverride = false
                snackbarHostState.showSuccess(context.getString(R.string.override_removed))
            } else {
                val message = (result.output + result.error).firstOrNull() ?: context.getString(R.string.failed_to_remove_override)
                snackbarHostState.showError(message)
            }
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    // Title carries a unit name, so it stays small enough to survive one line.
                    Text(
                        context.getString(R.string.edit_override_title, unitName),
                        style = MaterialTheme.typography.titleSmall,
                        fontWeight = FontWeight.Bold,
                        maxLines = 1
                    )
                },
                navigationIcon = {
                    IconButton(onClick = onNavigateBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, context.getString(R.string.back))
                    }
                },
                actions = {
                    if (hasExistingOverride) {
                        IconButton(onClick = { delete() }, enabled = !isSaving) {
                            Icon(Icons.Default.Delete, context.getString(R.string.delete_override))
                        }
                    }
                    IconButton(onClick = { save() }, enabled = !isSaving) {
                        Icon(Icons.Default.Save, context.getString(R.string.save_override))
                    }
                },
                colors = TopAppBarDefaults.topAppBarColors(containerColor = Color.Transparent)
            )
        },
        snackbarHost = { DsSnackbarHost(snackbarHostState) }
    ) { padding ->
        Box(modifier = Modifier.padding(padding).fillMaxSize().padding(16.dp)) {
            if (isLoading) {
                FullScreenLoading()
            } else {
                OutlinedTextField(
                    value = text,
                    onValueChange = { text = it },
                    modifier = Modifier.fillMaxSize(),
                    textStyle = MaterialTheme.typography.bodyMedium.copy(fontFamily = JetBrainsMono),
                    shape = RoundedCornerShape(16.dp),
                    placeholder = { Text(context.getString(R.string.override_placeholder)) }
                )
            }
        }
    }

    if (isSaving) {
        ProgressDialog(message = context.getString(if (hasExistingOverride) R.string.saving_override else R.string.applying_override))
    }
}