package com.droidspaces.app.ui.component

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.*
import androidx.compose.animation.*
import androidx.compose.animation.core.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ContentCopy
import androidx.compose.material.icons.filled.Person
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import com.droidspaces.app.ui.component.DsDropdown
import androidx.compose.ui.platform.LocalContext
import com.droidspaces.app.ui.util.rememberClearFocus
import androidx.compose.ui.unit.dp
import com.droidspaces.app.util.Constants
import com.droidspaces.app.util.ContainerCommandBuilder
import com.droidspaces.app.util.PreferencesManager
import com.droidspaces.app.ui.util.LoadingIndicator
import com.droidspaces.app.ui.util.LoadingSize
import com.droidspaces.app.ui.util.showSuccess
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import com.droidspaces.app.R
import androidx.compose.ui.graphics.vector.rememberVectorPainter

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ContainerUsersCard(
    containerName: String,
    users: List<String>?,
    onRefresh: suspend () -> Unit,
    snackbarHostState: SnackbarHostState,
    modifier: Modifier = Modifier
) {
    val scope = rememberCoroutineScope()
    val context = LocalContext.current
    val clearFocus = rememberClearFocus()

    // Null means the screen is still probing the container
    val isLoading = users == null

    // Available users list: always include "root", plus any local users
    val availableUsers = remember(users) {
        val userList = users.orEmpty().toMutableList()
        if (!userList.contains("root")) {
            userList.add(0, "root") // Add root at the beginning if not present
        }
        userList
    }

    // Same preselection as the terminal's user picker
    var selectedUser by remember {
        mutableStateOf(PreferencesManager.getInstance(context).getLastTerminalUser(containerName) ?: "root")
    }

    // Skipped while loading, or the remembered user is reset before the list arrives
    LaunchedEffect(users) {
        if (!isLoading && !availableUsers.contains(selectedUser)) {
            selectedUser = "root"
        }
    }


    DetailCard(
        icon = rememberVectorPainter(Icons.Default.Person),
        title = context.getString(R.string.available_users),
        // A tap on the card outside its field drops the keyboard.
        modifier = modifier
            .animateContentSize()
            .clickable(indication = null, interactionSource = remember { MutableInteractionSource() }) { clearFocus() },
        action = {
            DsRefreshButton(
                contentDescription = context.getString(R.string.refresh_users),
                onRefresh = {
                    clearFocus()
                    onRefresh()
                },
                modifier = Modifier.size(40.dp),
                iconSize = 20.dp,
                tint = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    ) {
        // Show users list - use stable layout to prevent glitches
        if (isLoading) {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .heightIn(min = 40.dp),
                contentAlignment = Alignment.Center
            ) {
                LoadingIndicator(size = LoadingSize.Medium)
            }
        } else if (users.isNullOrEmpty()) {
            Text(
                text = context.getString(R.string.no_local_users_found),
                style = MaterialTheme.typography.bodyLarge,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.heightIn(min = 24.dp)
            )
        } else {
            Column(
                verticalArrangement = Arrangement.spacedBy(8.dp),
                modifier = Modifier
                    .heightIn(max = 150.dp)
                    .clickable(
                        indication = null,
                        interactionSource = remember { MutableInteractionSource() }
                    ) {
                        clearFocus()
                    }
            ) {
                users.orEmpty().forEach { user ->
                    Text(text = user, style = MaterialTheme.typography.bodyMedium)
                }
            }
        }

        DsDropdown(
            label = context.getString(R.string.select_user),
            selected = selectedUser,
            options = availableUsers,
            displayName = { it },
            onSelect = { selectedUser = it }
        )

        // Right aligned, the same button as the Manage card's
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.End
        ) {
            CardButton(context.getString(R.string.copy_login), Icons.Default.ContentCopy) {
                clearFocus()
                scope.launch {
                    // Check if droidspaces is in PATH, otherwise use full path
                    val droidspacesCmd = withContext(Dispatchers.IO) {
                        Constants.getDroidspacesCommand()
                    }

                    // Quote container name with double quotes
                    val quotedContainerName = "\"${containerName.replace("\"", "\\\"")}\""
                    val loginCommand = if (selectedUser == "root") {
                        "$droidspacesCmd --name=$quotedContainerName enter"
                    } else {
                        "$droidspacesCmd --name=$quotedContainerName enter $selectedUser"
                    }
                    // Wrap entire command in single quotes for su -c
                    val suCommand = "su -c '$loginCommand'"
                    val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
                    val clip = ClipData.newPlainText(context.getString(R.string.login_command_copied, selectedUser), suCommand)
                    clipboard.setPrimaryClip(clip)

                    snackbarHostState.showSuccess(context.getString(R.string.login_command_copied, selectedUser))
                }
            }
        }
    }
}
