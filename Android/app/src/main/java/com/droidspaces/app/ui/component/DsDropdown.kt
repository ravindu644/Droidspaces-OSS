package com.droidspaces.app.ui.component

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Check
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.luminance
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.focus.focusProperties
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun <T> DsDropdown(
    label: String,
    selected: T,
    options: List<T>,
    displayName: (T) -> String,
    onSelect: (T) -> Unit,
    modifier: Modifier = Modifier,
    leadingIcon: ImageVector? = null,
    isError: Boolean = false,
    supportingText: String? = null,
    enabled: Boolean = true
) {
    var expanded by remember { mutableStateOf(false) }
    // The menu fades out after a pick, and moving the check during that fade shifts two rows
    // at once. The open menu shows the selection it opened with, the field shows the new one.
    var menuSelected by remember { mutableStateOf(selected) }

    val fieldShape = RoundedCornerShape(16.dp)
    val fieldColors = DsTextFieldDefaults.colors()

    ExposedDropdownMenuBox(
        expanded = expanded && enabled,
        onExpandedChange = {
            if (!enabled) return@ExposedDropdownMenuBox
            if (it) menuSelected = selected
            expanded = it
        },
        modifier = modifier.fillMaxWidth()
    ) {
        OutlinedTextField(
            value = displayName(selected),
            onValueChange = {},
            readOnly = true,
            enabled = enabled,
            isError = isError,
            label = { Text(label) },
            supportingText = supportingText?.let { { Text(it) } },
            trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = expanded) },
            leadingIcon = leadingIcon?.let { icon -> { Icon(icon, contentDescription = null) } },
            shape = fieldShape,
            colors = fieldColors,
            modifier = Modifier
                .menuAnchor(ExposedDropdownMenuAnchorType.PrimaryNotEditable)
                // The text field takes focus from a tap only after the double-tap timeout, so a
                // tap that closes the menu would re-focus it and leave it outlined. Tying focus
                // to the menu drops it on close and refuses that late request.
                .focusProperties { canFocus = expanded }
                .fillMaxWidth()
        )
        DsMenuTheme {
            ExposedDropdownMenu(
                expanded = expanded,
                onDismissRequest = { expanded = false },
                modifier = Modifier.dsMenuBorder()
            ) {
                options.forEach { option ->
                    DropdownMenuItem(
                        text = { Text(displayName(option), fontWeight = FontWeight.Medium) },
                        onClick = {
                            onSelect(option)
                            expanded = false
                        },
                        leadingIcon = if (option == menuSelected) {
                            {
                                Icon(
                                    Icons.Default.Check,
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
}
