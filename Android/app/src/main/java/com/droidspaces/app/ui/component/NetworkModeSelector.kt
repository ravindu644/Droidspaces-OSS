package com.droidspaces.app.ui.component

import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Public
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.droidspaces.app.R

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun NetworkModeSelector(
    netMode: String,
    onModeChange: (String) -> Unit,
    modifier: Modifier = Modifier
) {
    val context = LocalContext.current
    var expanded by remember { mutableStateOf(false) }
    
    val modes = listOf("host", "nat", "none")
    val modeNames = mapOf(
        "host" to context.getString(R.string.network_mode_host),
        "nat" to context.getString(R.string.network_mode_nat),
        "none" to context.getString(R.string.network_mode_none)
    )

    val modernFieldShape = RoundedCornerShape(16.dp)
    val modernFieldColors = OutlinedTextFieldDefaults.colors(
        unfocusedBorderColor = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f),
        focusedBorderColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.8f),
        unfocusedContainerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.3f),
        focusedContainerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f)
    )

    ExposedDropdownMenuBox(
        expanded = expanded,
        onExpandedChange = { expanded = !expanded },
        modifier = modifier.fillMaxWidth()
    ) {
        OutlinedTextField(
            value = modeNames[netMode] ?: netMode,
            onValueChange = {},
            readOnly = true,
            label = { Text(context.getString(R.string.network_mode)) },
            trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = expanded) },
            leadingIcon = { Icon(Icons.Default.Public, contentDescription = null) },
            shape = modernFieldShape,
            colors = modernFieldColors,
            modifier = Modifier.menuAnchor().fillMaxWidth()
        )
        ExposedDropdownMenu(
            expanded = expanded,
            onDismissRequest = { expanded = false }
        ) {
            modes.forEach { mode ->
                DropdownMenuItem(
                    text = { Text(modeNames[mode] ?: mode, fontWeight = FontWeight.Medium) },
                    onClick = {
                        onModeChange(mode)
                        expanded = false
                    },
                    leadingIcon = if (mode == netMode) {
                        { 
                            Icon(
                                imageVector = Icons.Default.Check, 
                                contentDescription = null, 
                                modifier = Modifier.size(18.dp),
                                tint = MaterialTheme.colorScheme.primary
                            ) 
                        }
                    } else null
                )
            }
        }
    }
}
