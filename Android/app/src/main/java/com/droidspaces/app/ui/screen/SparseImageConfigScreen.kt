package com.droidspaces.app.ui.screen

import com.droidspaces.app.ui.component.SectionLabel
import com.droidspaces.app.ui.component.DsTextFieldDefaults
import com.droidspaces.app.ui.component.FilePickerDialog
import com.droidspaces.app.ui.component.SettingsCard
import com.droidspaces.app.ui.util.LoadingIndicator
import com.droidspaces.app.ui.util.LoadingSize
import com.droidspaces.app.util.Constants
import com.droidspaces.app.util.StorageChecker
import com.droidspaces.app.util.StorageSupport
import com.droidspaces.app.util.StorageTarget

import com.droidspaces.app.ui.component.PrimaryActionBottomBar
import androidx.compose.ui.graphics.Color
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.consumeWindowInsets

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.ArrowForward
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.platform.LocalContext
import com.droidspaces.app.R

@OptIn(ExperimentalMaterial3Api::class, ExperimentalLayoutApi::class)
@Composable
fun SparseImageConfigScreen(
    initialUseSparseImage: Boolean = true,
    initialSizeGB: Int = 8,
    initialStorageDir: String? = null,
    onNext: (useSparseImage: Boolean, sizeGB: Int, storageDir: String?) -> Unit,
    onBack: () -> Unit
) {
    val context = LocalContext.current
    var useSparseImage by remember { mutableStateOf(initialUseSparseImage) }
    var sizeGB by remember { mutableStateOf(initialSizeGB.toString()) }
    var sizeError by remember { mutableStateOf<String?>(null) }

    var storageDir by remember { mutableStateOf(initialStorageDir) }
    var showStoragePicker by remember { mutableStateOf(false) }
    var target by remember { mutableStateOf<StorageTarget?>(null) }
    var probing by remember { mutableStateOf(false) }

    // The filesystem decides which rootfs layouts are legal, so probe whatever the
    // destination currently is, including the default one.
    LaunchedEffect(storageDir) {
        probing = true
        target = StorageChecker.probe(storageDir ?: Constants.CONTAINERS_BASE_PATH)
        probing = false
        // A directory rootfs cannot live on a filesystem without the permission model.
        if (target?.support == StorageSupport.IMAGE_ONLY) useSparseImage = true
    }

    val fieldShape = RoundedCornerShape(16.dp)
    val fieldColors = DsTextFieldDefaults.colors()

    val destinationUsable = target != null && target?.support != StorageSupport.UNUSABLE
    val directoryModeAllowed = target?.support == StorageSupport.FULL
    val sizeValid = !useSparseImage || (sizeGB.toIntOrNull()?.let { it in 4..512 } == true)

    // A volume too small to hold a container is as much a blocker as one that cannot
    // represent it, so it gates Next the same way. Unknown free space is not treated as
    // insufficient; the installer warns and lets the extraction report the real error.
    val freeGB = target?.freeGB
    val spaceShort = freeGB != null && freeGB < Constants.MIN_STORAGE_GB

    val isNextEnabled = !probing && destinationUsable && sizeValid && !spaceShort

    Scaffold(
        containerColor = Color.Transparent,
        topBar = {
            TopAppBar(
                title = { Text(context.getString(R.string.storage_title), style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold) },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = context.getString(R.string.back))
                    }
                }
            )
        },
        bottomBar = {
            PrimaryActionBottomBar(
                label = context.getString(R.string.next_summary),
                icon = Icons.AutoMirrored.Filled.ArrowForward,
                onClick = {
                    if (useSparseImage) {
                        val s = sizeGB.toIntOrNull()
                        if (s != null && s in 4..512) onNext(true, s, storageDir)
                    } else {
                        onNext(false, 8, storageDir)
                    }
                },
                enabled = isNextEnabled
            )
        }
    ) { innerPadding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
                .consumeWindowInsets(innerPadding)
                .imePadding()
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 24.dp)
                .padding(top = 8.dp),
            verticalArrangement = Arrangement.spacedBy(20.dp)
        ) {
            if (showStoragePicker) {
                FilePickerDialog(
                    onDismiss = { showStoragePicker = false },
                    onConfirm = { picked ->
                        storageDir = picked.trimEnd('/').ifEmpty { "/" }
                        showStoragePicker = false
                    },
                    title = context.getString(R.string.storage_location_pick),
                    showFiles = false
                )
            }

            Text(
                text = context.getString(R.string.storage_configuration),
                style = MaterialTheme.typography.headlineSmall,
                fontWeight = FontWeight.Bold
            )

            SectionLabel(context.getString(R.string.storage_location))

            // Storage location. Built from SettingsCard so the two options line up with
            // each other and with every other option card in the app; a bare RadioButton
            // sits 10dp inside its own touch target and will not align with an icon.
            SettingsCard(
                title = context.getString(R.string.storage_location_default),
                onClick = { storageDir = null },
                icon = Icons.Default.PhoneAndroid,
                subtitleContent = {
                    Text(
                        text = context.getString(
                            R.string.storage_location_default_description,
                            Constants.CONTAINERS_BASE_PATH
                        ),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.8f)
                    )
                },
                trailing = {
                    RadioButton(selected = storageDir == null, onClick = null)
                }
            )

            SettingsCard(
                title = storageDir ?: context.getString(R.string.storage_location_custom),
                onClick = { showStoragePicker = true },
                icon = Icons.Default.SdStorage,
                subtitleContent = {
                    Text(
                        text = storageDir?.let { context.getString(R.string.storage_location_change_hint) }
                            ?: context.getString(R.string.storage_location_custom_description),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.8f)
                    )
                },
                trailing = {
                    RadioButton(selected = storageDir != null, onClick = null)
                }
            )

            // What the probe found. This is the only place the user learns why a volume
            // refuses directory mode, or refuses outright, so it is styled to be read
            // rather than tucked under the field as fine print.
            val probed = target
            when {
                probing -> StorageNoticeCard(
                    icon = null,
                    headline = context.getString(R.string.storage_location_checking),
                    detail = null,
                    accent = MaterialTheme.colorScheme.primary,
                    container = MaterialTheme.colorScheme.primaryContainer
                )
                probed != null -> {
                    val headline = probed.freeGB?.let {
                        context.getString(R.string.storage_location_free, probed.fsType, it)
                    } ?: context.getString(R.string.storage_location_fs_only, probed.fsType)
                    when {
                        probed.support == StorageSupport.UNUSABLE -> StorageNoticeCard(
                            icon = Icons.Default.Error,
                            headline = headline,
                            detail = probed.reason,
                            accent = MaterialTheme.colorScheme.error,
                            container = MaterialTheme.colorScheme.errorContainer
                        )
                        // Ranked above IMAGE_ONLY: if the volume is too small, being told
                        // it only takes an image is not the thing that needs fixing.
                        spaceShort -> StorageNoticeCard(
                            icon = Icons.Default.Error,
                            headline = headline,
                            detail = context.getString(
                                R.string.storage_location_needs_space, Constants.MIN_STORAGE_GB
                            ),
                            accent = MaterialTheme.colorScheme.error,
                            container = MaterialTheme.colorScheme.errorContainer
                        )
                        probed.support == StorageSupport.IMAGE_ONLY -> StorageNoticeCard(
                            icon = Icons.Default.Warning,
                            headline = headline,
                            detail = probed.reason,
                            accent = MaterialTheme.colorScheme.tertiary,
                            container = MaterialTheme.colorScheme.tertiaryContainer
                        )
                        else -> StorageNoticeCard(
                            icon = Icons.Default.CheckCircle,
                            headline = headline,
                            detail = null,
                            accent = MaterialTheme.colorScheme.primary,
                            container = MaterialTheme.colorScheme.primaryContainer
                        )
                    }
                }
            }

            HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.4f))

            SectionLabel(context.getString(R.string.sparse_image_section))

            // Info card
            Surface(
                modifier = Modifier.fillMaxWidth(),
                shape = RoundedCornerShape(20.dp),
                color = MaterialTheme.colorScheme.primary.copy(alpha = 0.06f),
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.primary.copy(alpha = 0.2f)),
                tonalElevation = 0.dp
            ) {
                Column(
                    modifier = Modifier.padding(20.dp),
                    verticalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        Icon(
                            imageVector = Icons.Default.Info,
                            contentDescription = null,
                            modifier = Modifier.size(20.dp),
                            tint = MaterialTheme.colorScheme.primary
                        )
                        Text(
                            text = context.getString(R.string.about_sparse_images),
                            style = MaterialTheme.typography.titleMedium,
                            fontWeight = FontWeight.SemiBold,
                            color = MaterialTheme.colorScheme.primary
                        )
                    }
                    Text(
                        text = context.getString(R.string.sparse_images_recommended),
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Text(
                        text = context.getString(R.string.sparse_images_description),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.8f)
                    )
                }
            }

            // Toggle
            Surface(
                modifier = Modifier.fillMaxWidth(),
                onClick = { if (directoryModeAllowed) useSparseImage = !useSparseImage },
                enabled = directoryModeAllowed,
                shape = RoundedCornerShape(20.dp),
                color = MaterialTheme.colorScheme.surfaceContainerHigh,
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f)),
                tonalElevation = 0.dp
            ) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(20.dp),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(16.dp),
                        modifier = Modifier.weight(1f)
                    ) {
                        Icon(
                            imageVector = Icons.Default.Storage,
                            contentDescription = null,
                            modifier = Modifier.size(24.dp),
                            tint = MaterialTheme.colorScheme.primary
                        )
                        Column(
                            modifier = Modifier.weight(1f),
                            verticalArrangement = Arrangement.spacedBy(4.dp)
                        ) {
                            Text(
                                text = context.getString(R.string.use_sparse_image),
                                style = MaterialTheme.typography.titleMedium,
                                fontWeight = FontWeight.SemiBold
                            )
                            Text(
                                text = if (directoryModeAllowed) {
                                    context.getString(R.string.use_sparse_image_description)
                                } else {
                                    context.getString(R.string.storage_location_image_forced)
                                },
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
                            )
                        }
                    }
                    Switch(
                        checked = useSparseImage,
                        enabled = directoryModeAllowed,
                        onCheckedChange = { useSparseImage = it },
                        colors = SwitchDefaults.colors(
                            checkedThumbColor = MaterialTheme.colorScheme.onPrimary,
                            checkedTrackColor = MaterialTheme.colorScheme.primary,
                            uncheckedThumbColor = MaterialTheme.colorScheme.outline.copy(alpha = 0.8f),
                            uncheckedTrackColor = MaterialTheme.colorScheme.surfaceVariant,
                            uncheckedBorderColor = MaterialTheme.colorScheme.outline.copy(alpha = 0.2f)
                        )
                    )
                }
            }

            // Size input
            if (useSparseImage) {
                Surface(
                    modifier = Modifier.fillMaxWidth(),
                    shape = RoundedCornerShape(20.dp),
                    color = MaterialTheme.colorScheme.surfaceContainerHigh,
                    border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f)),
                    tonalElevation = 0.dp
                ) {
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(20.dp),
                        verticalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(12.dp)
                        ) {
                            Icon(
                                imageVector = Icons.Default.Settings,
                                contentDescription = null,
                                modifier = Modifier.size(20.dp),
                                tint = MaterialTheme.colorScheme.primary
                            )
                            Text(
                                text = context.getString(R.string.image_size),
                                style = MaterialTheme.typography.titleMedium,
                                fontWeight = FontWeight.SemiBold
                            )
                        }

                        OutlinedTextField(
                            value = sizeGB,
                            onValueChange = { newValue ->
                                sizeGB = newValue
                                val sizeInt = newValue.toIntOrNull()
                                sizeError = when {
                                    newValue.isEmpty() -> context.getString(R.string.size_required)
                                    sizeInt == null -> context.getString(R.string.invalid_number)
                                    sizeInt < 4 -> context.getString(R.string.minimum_size_4gb)
                                    sizeInt > 512 -> context.getString(R.string.maximum_size_512gb)
                                    else -> null
                                }
                            },
                            label = { Text(context.getString(R.string.size_gb)) },
                            placeholder = { Text(context.getString(R.string.default_size_gb_hint)) },
                            isError = sizeError != null,
                            supportingText = sizeError?.let { { Text(it) } } ?: {
                                Text(context.getString(R.string.enter_size_between_4_512_gb))
                            },
                            modifier = Modifier.fillMaxWidth(),
                            singleLine = true,
                            shape = fieldShape,
                            colors = fieldColors,
                            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                            leadingIcon = {
                                Icon(Icons.Default.Settings, contentDescription = null)
                            }
                        )
                    }
                }
            }

            Spacer(modifier = Modifier.height(8.dp))
        }
    }
}

/**
 * The probe verdict for the chosen destination. Colour carries the severity, so an
 * unusable volume reads as a blocker and an image-only one as a constraint, without
 * either being mistaken for the neutral free-space line.
 *
 * Shaped after the "Container is running" panel in EditContainerScreen: a tinted wash of
 * the container colour behind an accent-coloured border, rather than a solid fill.
 */
@Composable
private fun StorageNoticeCard(
    icon: androidx.compose.ui.graphics.vector.ImageVector?,
    headline: String,
    detail: String?,
    accent: Color,
    container: Color,
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(20.dp),
        color = container.copy(alpha = 0.2f),
        border = BorderStroke(1.dp, accent.copy(alpha = 0.3f)),
        tonalElevation = 0.dp
    ) {
        Row(
            modifier = Modifier.padding(20.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            if (icon != null) {
                Icon(
                    imageVector = icon,
                    contentDescription = null,
                    modifier = Modifier.size(24.dp),
                    tint = accent
                )
            } else {
                LoadingIndicator(LoadingSize.Medium, color = accent)
            }
            Column {
                Text(
                    text = headline,
                    style = MaterialTheme.typography.titleSmall,
                    fontWeight = FontWeight.Bold,
                    color = accent
                )
                if (detail != null) {
                    Text(
                        text = detail,
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
                    )
                }
            }
        }
    }
}

