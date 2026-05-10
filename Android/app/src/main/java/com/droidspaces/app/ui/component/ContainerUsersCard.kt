package com.droidspaces.app.ui.component

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.animation.*
import androidx.compose.animation.core.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ContentCopy
import androidx.compose.material.icons.filled.Person
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import com.droidspaces.app.ui.util.rememberClearFocus
import androidx.compose.ui.unit.dp
import com.droidspaces.app.util.Constants
import com.droidspaces.app.util.ContainerCommandBuilder
import com.droidspaces.app.util.ContainerUsersManager
import com.droidspaces.app.ui.util.LoadingIndicator
import com.droidspaces.app.ui.util.LoadingSize
import com.droidspaces.app.ui.util.showSuccess
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import com.droidspaces.app.R

@OptIn(ExperimentalMaterial3Api::class)
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
    var isManuallyRefreshing by remember { mutableStateOf(false) }

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

    // Dropdown expanded state - single source of truth
    var isDropdownExpanded by remember { mutableStateOf(false) }

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
            isManuallyRefreshing = false
        }
    }

    // Unified unfocus function - prevents race conditions
    fun unfocusDropdown() {
        if (isDropdownExpanded) {
            isDropdownExpanded = false
        }
        clearFocus()
    }

    // Manual refresh function with premium rotation animation
    fun refreshUsers() {
        if (!isManuallyRefreshing) {
            isManuallyRefreshing = true
            scope.launch {
                // Start timing for artificial delay
                val startTime = System.currentTimeMillis()

                // Fetch fresh data
                users = ContainerUsersManager.getUsers(containerName, useCache = false)

                // Premium UX: Ensure icon completes exactly 1 full rotation (600ms)
                val elapsed = System.currentTimeMillis() - startTime
                val minRotationTime = 600L // One complete 360° rotation
                if (elapsed < minRotationTime) {
                    delay(minRotationTime - elapsed)
                }

                isManuallyRefreshing = false
            }
        }
    }

    Surface(
        modifier = modifier
            .fillMaxWidth()
            .animateContentSize(), // Smooth height changes when dropdown opens
        shape = RoundedCornerShape(20.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f),
        border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f))
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                // Title area - clickable to unfocus
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(10.dp),
                    modifier = Modifier.clickable(
                        indication = null,
                        interactionSource = remember { MutableInteractionSource() }
                    ) {
                        clearFocus()
                    }
                ) {
                    Icon(
                        Icons.Default.Person,
                        contentDescription = null,
                        modifier = Modifier.size(20.dp),
                        tint = MaterialTheme.colorScheme.primary
                    )
                    Text(
                        text = context.getString(R.string.available_users),
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colorScheme.onSurface
                    )
                }

                // Premium refresh icon button - exactly 1 full rotation, perfectly visible!
                val rotation by animateFloatAsState(
                    targetValue = if (isManuallyRefreshing) 360f else 0f,
                    animationSpec = if (isManuallyRefreshing) {
                        // Animate forward smoothly - 600ms for one full rotation
                        tween(
                            durationMillis = 600,
                            easing = LinearEasing
                        )
                    } else {
                        // Snap back instantly when done (no reverse animation)
                        tween(
                            durationMillis = 0,
                            easing = LinearEasing
                        )
                    },
                    label = "refresh_rotation"
                )

                IconButton(
                    onClick = {
                        clearFocus()
                        refreshUsers()
                    },
                    enabled = !isManuallyRefreshing,
                    modifier = Modifier.size(40.dp)
                ) {
                    Icon(
                        Icons.Default.Refresh,
                        contentDescription = context.getString(R.string.refresh_users),
                        modifier = Modifier
                            .size(20.dp)
                            .graphicsLayer {
                                // Hardware-accelerated rotation for buttery smooth 60fps
                                rotationZ = rotation
                            },
                        tint = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }

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
            } else if (users.isEmpty()) {
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
                            unfocusDropdown()
                        }
                ) {
                    users.forEach { user ->
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text(
                                text = user,
                                style = MaterialTheme.typography.bodyMedium
                            )
                        }
                    }
                }
            }

            // User selection dropdown - optimized with single source of truth
            ExposedDropdownMenuBox(
                expanded = isDropdownExpanded,
                onExpandedChange = { newExpanded ->
                    // Direct state update - no conditional logic to prevent race conditions
                    isDropdownExpanded = newExpanded
                    // Clear focus when closing
                    if (!newExpanded) {
                        clearFocus()
                    }
                },
                modifier = Modifier
                    .fillMaxWidth()
                    .heightIn(min = 56.dp)
            ) {
                OutlinedTextField(
                    value = selectedUser,
                    onValueChange = {},
                    readOnly = true,
                    label = { Text(context.getString(R.string.select_user)) },
                    trailingIcon = {
                        ExposedDropdownMenuDefaults.TrailingIcon(expanded = isDropdownExpanded)
                    },
                    shape = RoundedCornerShape(16.dp),
                    colors = OutlinedTextFieldDefaults.colors(
                        unfocusedBorderColor = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f),
                        focusedBorderColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.8f),
                        unfocusedContainerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.3f),
                        focusedContainerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f)
                    ),
                modifier = Modifier
                    .menuAnchor()
                    .fillMaxWidth()
                    .onFocusChanged { focusState ->
                        // Handle focus loss - close dropdown immediately for smooth animation
                        // No delay needed - direct state update prevents race conditions
                        if (!focusState.isFocused && isDropdownExpanded) {
                            isDropdownExpanded = false
                        }
                    }
                )
                ExposedDropdownMenu(
                    expanded = isDropdownExpanded,
                    onDismissRequest = {
                        unfocusDropdown()
                    },
                    modifier = Modifier.heightIn(max = 200.dp)
                ) {
                    availableUsers.forEach { user ->
                        DropdownMenuItem(
                            text = { Text(user) },
                            onClick = {
                                selectedUser = user
                                unfocusDropdown()
                            }
                        )
                    }
                }
            }

            // Copy login button - right aligned, same dimensions and style as Manage button
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.End
            ) {
                Button(
                    onClick = {
                        unfocusDropdown()
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
                    },
                    modifier = Modifier.widthIn(min = 140.dp),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = MaterialTheme.colorScheme.primary,
                        contentColor = MaterialTheme.colorScheme.onPrimary
                    )
                ) {
                    Icon(
                        Icons.Default.ContentCopy,
                        contentDescription = null,
                        modifier = Modifier.size(18.dp)
                    )
                    Spacer(Modifier.width(6.dp))
                    Text(
                        context.getString(R.string.copy_login),
                        fontWeight = FontWeight.SemiBold
                    )
                }
            }
        }
    }
}

