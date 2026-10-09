package com.droidspaces.app.ui.component

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import androidx.compose.foundation.layout.*
import androidx.compose.animation.*
import androidx.compose.animation.core.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.ContentCopy
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import com.droidspaces.app.ui.util.rememberClearFocus
import androidx.compose.ui.unit.dp
import com.droidspaces.app.util.Constants
import com.droidspaces.app.util.ContainerCommandBuilder
import com.droidspaces.app.util.ContainerUsersManager
import com.droidspaces.app.ui.util.LoadingIndicator
import com.droidspaces.app.ui.util.LoadingSize
import com.droidspaces.app.ui.util.showSuccess
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import com.droidspaces.app.R

@OptIn(ExperimentalMaterial3Api::class, ExperimentalMaterial3ExpressiveApi::class, ExperimentalLayoutApi::class)
@Composable
fun ContainerUsersCard(
    containerName: String,
    refreshTrigger: Int = 0,
    snackbarHostState: SnackbarHostState? = null,
    modifier: Modifier = Modifier
) {
    val scope = rememberCoroutineScope()
    val context = LocalContext.current
    val clearFocus = rememberClearFocus()

    // Use cached value initially for instant display
    var users by remember {
        mutableStateOf<List<String>>(
            ContainerUsersManager.getCachedUsers(containerName) ?: emptyList()
        )
    }
    var isLoading by remember {
        mutableStateOf(ContainerUsersManager.getCachedUsers(containerName) == null)
    }

    // Available users list: always include "root", plus any local users
    val availableUsers = remember(users) {
        val userList = users.toMutableList()
        if (!userList.contains("root")) {
            userList.add(0, "root") // Add root at the beginning if not present
        }
        userList
    }

    // Selected user state - default to "root"
    var selectedUser by remember { mutableStateOf("root") }

    // Update selected user when users list changes (keep current selection if still valid)
    LaunchedEffect(users) {
        if (!availableUsers.contains(selectedUser)) {
            selectedUser = "root"
        }
    }

    LaunchedEffect(containerName, refreshTrigger) {
        scope.launch {
            // Only show loading if we don't have cached data (first load)
            // Don't show loading on refresh to prevent glitches
            val shouldShowLoading = users.isEmpty() && refreshTrigger == 0
            if (shouldShowLoading) {
                isLoading = true
            }
            users = ContainerUsersManager.getUsers(containerName, useCache = refreshTrigger == 0)
            isLoading = false
        }
    }

    // The shell user, picked from a connected toggle group: root first, then the
    // container's own users. The section card around this is the caller's.
    Column(modifier = modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(16.dp)) {
        if (isLoading) {
            LoadingIndicator(size = LoadingSize.Medium, modifier = Modifier.align(Alignment.CenterHorizontally))
        } else {
            // FlowRow so a container with many users wraps instead of squeezing
            FlowRow(
                horizontalArrangement = Arrangement.spacedBy(ButtonGroupDefaults.ConnectedSpaceBetween),
                verticalArrangement = Arrangement.spacedBy(ButtonGroupDefaults.ConnectedSpaceBetween)
            ) {
                availableUsers.forEachIndexed { i, user ->
                    val checked = user == selectedUser
                    ToggleButton(
                        checked = checked,
                        onCheckedChange = { selectedUser = user },
                        shapes = when {
                            availableUsers.size == 1 -> ToggleButtonDefaults.shapes()
                            i == 0 -> ButtonGroupDefaults.connectedLeadingButtonShapes()
                            i == availableUsers.lastIndex -> ButtonGroupDefaults.connectedTrailingButtonShapes()
                            else -> ButtonGroupDefaults.connectedMiddleButtonShapes()
                        },
                        // The default unchecked fill is surfaceContainer, the card's own colour,
                        // so it would vanish here. DESIGN.md: unchecked is secondaryContainer.
                        colors = ToggleButtonDefaults.toggleButtonColors(
                            containerColor = MaterialTheme.colorScheme.secondaryContainer,
                            contentColor = MaterialTheme.colorScheme.onSecondaryContainer
                        ),
                        modifier = Modifier.heightIn(min = 48.dp)
                    ) {
                        if (checked) {
                            Icon(Icons.Default.Check, contentDescription = null, modifier = Modifier.size(18.dp))
                            Spacer(Modifier.width(8.dp))
                        }
                        Text(user, style = MaterialTheme.typography.labelLarge)
                    }
                }
            }
        }

        // Copy login button - right aligned, same dimensions and style as Manage button
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.End
        ) {
            DetailPill(
                text = context.getString(R.string.copy_login),
                icon = Icons.Default.ContentCopy,
                iconSize = 18.dp,
                onClick = {
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

                        // Show snackbar feedback
                        snackbarHostState?.let { hostState ->
                            scope.launch {
                                hostState.showSuccess(context.getString(R.string.login_command_copied, selectedUser))
                            }
                        }
                    }
                }
            )
        }
    }
}
