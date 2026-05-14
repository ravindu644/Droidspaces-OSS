package com.droidspaces.app.ui.screen
import androidx.compose.ui.graphics.Color

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.ui.draw.clip
import androidx.compose.material.icons.automirrored.filled.ArrowForward
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import com.droidspaces.app.util.ValidationUtils
import com.droidspaces.app.util.ContainerManager
import com.droidspaces.app.ui.util.FocusUtils
import com.droidspaces.app.ui.util.rememberClearFocus
import androidx.compose.ui.platform.LocalContext
import com.droidspaces.app.R

@OptIn(ExperimentalMaterial3Api::class, ExperimentalLayoutApi::class)
@Composable
fun ContainerNameScreen(
    initialName: String = "",
    initialHostname: String = "",
    existingContainerNames: List<String> = emptyList(),
    onNext: (String, String) -> Unit,
    onClose: () -> Unit
) {
    var containerName by remember { mutableStateOf(initialName) }
    var hostname by remember { mutableStateOf(initialHostname) }
    var nameError by remember { mutableStateOf<String?>(null) }
    var hostnameError by remember { mutableStateOf<String?>(null) }

    val context = LocalContext.current
    val nameFocusRequester = remember { FocusRequester() }
    val hostnameFocusRequester = remember { FocusRequester() }
    val clearFocus = rememberClearFocus()

    // Request focus on container name field when screen appears
    LaunchedEffect(Unit) {
        if (containerName.isEmpty()) {
            nameFocusRequester.requestFocus()
        }
    }

    // Check for duplicate name when container name changes (instant check using cached names)
    LaunchedEffect(containerName, existingContainerNames) {
        if (containerName.isNotBlank()) {
            // Validate format first
            val formatResult = ValidationUtils.validateContainerName(containerName, context)
            if (formatResult.isError) {
                nameError = formatResult.errorMessage
            } else {
                // Check for duplicate - instant check using cached names
                // Compare both exact names and sanitized names
                // This handles cases where user enters "My Container" and there's "My-Container" or vice versa
                val sanitizedInput = ContainerManager.sanitizeContainerName(containerName)
                val isDuplicate = existingContainerNames.any { existingName ->
                    // Check exact match (case-insensitive)
                    existingName.equals(containerName, ignoreCase = true) ||
                    // Check sanitized match (handles spaces vs dashes)
                    ContainerManager.sanitizeContainerName(existingName).equals(sanitizedInput, ignoreCase = true)
                }
                if (isDuplicate) {
                    nameError = context.getString(R.string.container_name_exists)
                } else {
                    nameError = null
                }
            }
        } else {
            nameError = null
        }
    }

    Scaffold(
        containerColor = Color.Transparent,
        topBar = {
            TopAppBar(
                title = { Text(context.getString(R.string.container_setup)) },
                navigationIcon = {
                    IconButton(onClick = onClose) {
                        Icon(Icons.Default.Close, contentDescription = context.getString(R.string.close))
                    }
                }
            )
        },
        bottomBar = {
            val btnShape = RoundedCornerShape(20.dp)
            val isNextValid = containerName.isNotBlank() && nameError == null && hostnameError == null
            Surface(
                modifier = Modifier.fillMaxWidth(),
                color = MaterialTheme.colorScheme.surfaceContainer,
                tonalElevation = 0.dp
            ) {
                Column(modifier = Modifier.fillMaxWidth()) {
                    HorizontalDivider(
                        color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.25f),
                        thickness = 1.dp
                    )
                    Surface(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(24.dp)
                            .navigationBarsPadding()
                            .clip(btnShape)
                            .clickable(
                                enabled = isNextValid,
                                onClick = {
                                    clearFocus()
                                    val nameResult = ValidationUtils.validateContainerName(containerName, context)
                                    val hostnameResult = ValidationUtils.validateHostname(hostname.ifEmpty { containerName }, context)
                                    if (!nameResult.isError && !hostnameResult.isError) {
                                        onNext(containerName, hostname.ifEmpty { containerName })
                                    } else {
                                        nameError = nameResult.errorMessage
                                        hostnameError = hostnameResult.errorMessage
                                    }
                                },
                                indication = androidx.compose.material.ripple.rememberRipple(bounded = true),
                                interactionSource = remember { androidx.compose.foundation.interaction.MutableInteractionSource() }
                            ),
                        shape = btnShape,
                        color = if (isNextValid) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.12f),
                        tonalElevation = 0.dp
                    ) {
                        Box(modifier = Modifier.padding(vertical = 16.dp), contentAlignment = Alignment.Center) {
                            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                Icon(
                                    Icons.AutoMirrored.Filled.ArrowForward,
                                    contentDescription = null,
                                    modifier = Modifier.size(18.dp),
                                    tint = if (isNextValid) MaterialTheme.colorScheme.onPrimary else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f)
                                )
                                Text(
                                    context.getString(R.string.next_configuration),
                                    style = MaterialTheme.typography.labelLarge,
                                    fontWeight = FontWeight.SemiBold,
                                    color = if (isNextValid) MaterialTheme.colorScheme.onPrimary else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f)
                                )
                            }
                        }
                    }
                }
            }
        }
    ) { innerPadding ->
        Box(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
                .consumeWindowInsets(innerPadding)
                .imePadding()
                .clickable(
                    indication = null,
                    interactionSource = remember { MutableInteractionSource() }
                ) {
                    clearFocus()
                }
        ) {
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .verticalScroll(rememberScrollState())
                    .padding(horizontal = 24.dp)
                    .padding(top = 24.dp),
                verticalArrangement = Arrangement.spacedBy(24.dp)
            ) {
            Text(
                text = context.getString(R.string.container_information),
                style = MaterialTheme.typography.headlineSmall,
                fontWeight = FontWeight.Bold
            )

            // Container Name
            OutlinedTextField(
                value = containerName,
                onValueChange = {
                    containerName = it
                    // Validation will be handled by LaunchedEffect
                },
                label = { Text(context.getString(R.string.container_name_label)) },
                placeholder = { Text(context.getString(R.string.container_name_placeholder)) },
                isError = nameError != null,
                supportingText = nameError?.let { { Text(it) } },
                modifier = Modifier
                    .fillMaxWidth()
                    .focusRequester(nameFocusRequester),
                singleLine = true,
                keyboardOptions = KeyboardOptions(
                    imeAction = ImeAction.Next,
                    keyboardType = KeyboardType.Text
                ),
                keyboardActions = KeyboardActions(
                    onNext = {
                        if (nameError == null) {
                            hostnameFocusRequester.requestFocus()
                        }
                    }
                )
            )

            // Hostname
            OutlinedTextField(
                value = hostname,
                onValueChange = {
                    hostname = it
                    hostnameError = ValidationUtils.validateHostname(it, context).errorMessage
                },
                label = { Text(context.getString(R.string.hostname)) },
                placeholder = { Text(context.getString(R.string.hostname_placeholder)) },
                isError = hostnameError != null,
                supportingText = hostnameError?.let { { Text(it) } } ?: {
                    Text(context.getString(R.string.hostname_hint))
                },
                modifier = Modifier
                    .fillMaxWidth()
                    .focusRequester(hostnameFocusRequester),
                singleLine = true,
                keyboardOptions = FocusUtils.doneKeyboardOptions,
                keyboardActions = FocusUtils.clearFocusKeyboardActions()
            )

            Spacer(modifier = Modifier.height(16.dp))
            }
        }
    }
}

