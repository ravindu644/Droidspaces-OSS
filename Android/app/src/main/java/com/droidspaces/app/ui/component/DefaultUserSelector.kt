package com.droidspaces.app.ui.component

import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Person
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.droidspaces.app.util.ContainerUsersManager
import com.droidspaces.app.R

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DefaultUserSelector(
    defaultUser: String,
    containerName: String,
    onUserChange: (String) -> Unit,
    modifier: Modifier = Modifier
) {
    val context = LocalContext.current
    var expanded by remember { mutableStateOf(false) }
    var availableUsers by remember { mutableStateOf<List<String>>(emptyList()) }

    LaunchedEffect(Unit) {
        availableUsers = ContainerUsersManager.getCachedUsers(containerName) ?: emptyList()
    }

    val alwaysAskLabel = context.getString(R.string.always_ask)
    val alwaysAskEntry = "always_ask"

    // Add default user as it may not be there yet if container is down
    if (!availableUsers.contains(defaultUser)) availableUsers = availableUsers + listOf(defaultUser)
    // Add root at the beginning if not present
    if (!availableUsers.contains("root")) availableUsers = listOf("root") + availableUsers
    // Add "Always Ask" option at the beginning
    if (!availableUsers.contains(alwaysAskEntry)) availableUsers = listOf(alwaysAskEntry) + availableUsers

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
            value = if (defaultUser == alwaysAskEntry) alwaysAskLabel else defaultUser,
            onValueChange = {},
            readOnly = true,
            label = { Text(context.getString(R.string.default_user)) },
            trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = expanded) },
            leadingIcon = { Icon(Icons.Default.Person, contentDescription = null) },
            shape = modernFieldShape,
            colors = modernFieldColors,
            modifier = Modifier.menuAnchor().fillMaxWidth()
        )
        ExposedDropdownMenu(
            expanded = expanded,
            onDismissRequest = { expanded = false }
        ) {
            availableUsers.forEach { user ->
                DropdownMenuItem(
                    text = { Text(if (user == alwaysAskEntry) alwaysAskLabel else user, fontWeight = FontWeight.Medium) },
                    onClick = {
                        onUserChange(user)
                        expanded = false
                    },
                    leadingIcon = if (user == defaultUser) {
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
