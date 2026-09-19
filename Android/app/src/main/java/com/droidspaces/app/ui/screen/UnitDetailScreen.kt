package com.droidspaces.app.ui.screen

import android.content.Context
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Terminal
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.droidspaces.app.ui.theme.JetBrainsMono
import com.droidspaces.app.R
import com.droidspaces.app.ui.component.SectionHeader
import com.droidspaces.app.ui.util.FullScreenLoading
import com.droidspaces.app.util.ContainerSystemdManager
import kotlinx.coroutines.launch

/** Human-friendly label resources for the raw systemd property keys `inspectUnit` fetches. */
private val PROPERTY_LABEL_RES = linkedMapOf(
    "Description" to R.string.property_description,
    "LoadState" to R.string.property_load_state,
    "ActiveState" to R.string.property_active_state,
    "SubState" to R.string.property_sub_state,
    "UnitFileState" to R.string.property_enablement,
    "FragmentPath" to R.string.property_unit_file,
    "DropInPaths" to R.string.property_drop_ins,
    "MainPID" to R.string.property_main_pid,
    "ExecMainStartTimestamp" to R.string.property_started_at,
    "Restart" to R.string.property_restart_policy,
    "MemoryCurrent" to R.string.property_memory,
    "CPUUsageNSec" to R.string.property_cpu_time,
)

private sealed class UnitDetailState {
    data object Loading : UnitDetailState()
    data object Error : UnitDetailState()
    data class Ready(val inspection: ContainerSystemdManager.UnitInspection) : UnitDetailState()
}

/**
 * Read-only inspection screen for a single systemd unit: parsed properties,
 * raw `systemctl status` text, and its dependency tree. Reached from the
 * "Inspect unit" overflow-menu item on [SystemdScreen].
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun UnitDetailScreen(
    containerName: String,
    unitName: String,
    onNavigateBack: () -> Unit,
    onEditOverride: () -> Unit,
    onViewLogs: () -> Unit,
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var state by remember { mutableStateOf<UnitDetailState>(UnitDetailState.Loading) }

    fun load() {
        state = UnitDetailState.Loading
        scope.launch {
            val inspection = ContainerSystemdManager.inspectUnit(containerName, unitName)
            state = if (inspection != null) UnitDetailState.Ready(inspection) else UnitDetailState.Error
        }
    }

    LaunchedEffect(containerName, unitName) { load() }

    Surface(modifier = Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
        Scaffold(
            topBar = {
                TopAppBar(
                    title = {
                        // A unit name is machine text and runs long, so this stays mono and
                        // one step down from the titleLarge every other screen title uses.
                        Text(
                            unitName,
                            style = MaterialTheme.typography.titleMedium.copy(fontFamily = JetBrainsMono),
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
                        IconButton(onClick = { load() }) { Icon(Icons.Default.Refresh, context.getString(R.string.refresh)) }
                        IconButton(onClick = onViewLogs) { Icon(Icons.Default.Terminal, context.getString(R.string.view_logs)) }
                        IconButton(onClick = onEditOverride) { Icon(Icons.Default.Edit, context.getString(R.string.edit_override)) }
                    },
                    colors = TopAppBarDefaults.centerAlignedTopAppBarColors(containerColor = Color.Transparent)
                )
            },
            containerColor = Color.Transparent
        ) { padding ->
            Box(modifier = Modifier.padding(padding).fillMaxSize()) {
                when (val s = state) {
                    is UnitDetailState.Loading -> FullScreenLoading(message = context.getString(R.string.fetching_services))
                    is UnitDetailState.Error -> UnitDetailError(onRetry = { load() })
                    is UnitDetailState.Ready -> UnitDetailContent(s.inspection, context)
                }
            }
        }
    }
}

@Composable
private fun UnitDetailError(onRetry: () -> Unit) {
    val context = LocalContext.current
    Column(
        modifier = Modifier.fillMaxSize().padding(32.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        Text(context.getString(R.string.unit_detail_load_error_title), style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold)
        Spacer(Modifier.height(8.dp))
        Text(
            context.getString(R.string.unit_detail_load_error_message),
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            textAlign = TextAlign.Center
        )
        Spacer(Modifier.height(16.dp))
        FilledTonalButton(onClick = onRetry) { Text(context.getString(R.string.repo_retry)) }
    }
}

@Composable
private fun SectionCard(title: String, content: @Composable ColumnScope.() -> Unit) {
    Surface(
        modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp),
        shape = RoundedCornerShape(20.dp),
        color = MaterialTheme.colorScheme.surfaceContainer,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f))
    ) {
        Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            SectionHeader(title)
            content()
        }
    }
}

@Composable
private fun UnitDetailContent(inspection: ContainerSystemdManager.UnitInspection, context: Context) {
    LazyColumn(modifier = Modifier.fillMaxSize(), contentPadding = PaddingValues(vertical = 16.dp)) {
        item {
            SectionCard(title = context.getString(R.string.properties_section)) {
                PROPERTY_LABEL_RES.forEach { (key, labelRes) ->
                    val rawValue = inspection.properties[key]
                    val value = rawValue?.takeIf { it.isNotBlank() && it != "0" && it != "n/a" }
                    if (value != null) {
                        Row(
                            modifier = Modifier.fillMaxWidth().padding(vertical = 2.dp),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.Top
                        ) {
                            Text(
                                context.getString(labelRes),
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                modifier = Modifier.weight(0.4f)
                            )
                            Spacer(Modifier.width(12.dp))
                            Text(
                                if (key == "DropInPaths") value.replace(" ", "\n") else value,
                                style = MaterialTheme.typography.bodySmall.copy(
                                    fontFamily = JetBrainsMono,
                                    lineHeight = 16.sp
                                ),
                                fontWeight = FontWeight.Medium,
                                textAlign = TextAlign.End,
                                modifier = Modifier.weight(0.6f)
                            )
                        }
                    }
                }
            }
        }

        if (inspection.dependencies.isNotEmpty()) {
            item {
                SectionCard(title = context.getString(R.string.dependencies_section)) {
                    inspection.dependencies.forEach { dep ->
                        Text(dep, style = MaterialTheme.typography.bodySmall.copy(fontFamily = JetBrainsMono))
                    }
                }
            }
        }

        item {
            SectionCard(title = context.getString(R.string.systemctl_status_section)) {
                Surface(
                    color = MaterialTheme.colorScheme.surfaceContainerHighest,
                    shape = RoundedCornerShape(12.dp)
                ) {
                    Column(modifier = Modifier.padding(12.dp)) {
                        inspection.statusText.forEach { line ->
                            Text(line, style = MaterialTheme.typography.bodySmall.copy(fontFamily = JetBrainsMono))
                        }
                    }
                }
            }
        }
    }
}