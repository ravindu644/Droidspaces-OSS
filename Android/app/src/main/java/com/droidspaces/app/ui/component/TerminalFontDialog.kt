package com.droidspaces.app.ui.component

import android.graphics.Typeface
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.droidspaces.app.R
import com.droidspaces.app.ui.theme.JetBrainsMono
import com.droidspaces.app.util.FilePickerUtils
import com.droidspaces.app.util.FontInfo
import java.io.File
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

// The formats Typeface.createFromFile can load; .ttc loads its first face
private val FONT_EXTENSIONS = setOf("ttf", "otf", "ttc")

/**
 * Terminal font picker: the bundled default plus everything imported into
 * filesDir/fonts/, with an import action at the bottom. Imports and deletes
 * touch the disk immediately; the radio selection only lands when confirmed.
 */
@Composable
fun TerminalFontDialog(
    initialSelection: String,
    onConfirm: (String) -> Unit,
    onDismiss: () -> Unit,
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()

    // Import order, newest last; name only breaks mtime ties so the order
    // never shifts between visits
    fun listFonts() = FontInfo.fontsDir(context).listFiles()
        ?.filter { it.extension.lowercase() in FONT_EXTENSIONS }
        ?.sortedWith(compareBy({ it.lastModified() }, { it.name.lowercase() }))
        .orEmpty()

    var importedFonts by remember { mutableStateOf(listFonts()) }
    var selected by remember { mutableStateOf(initialSelection) }
    var importError by remember { mutableStateOf<String?>(null) }
    val fontNames = remember(importedFonts) {
        importedFonts.associate { it.name to FontInfo.displayName(it) }
    }
    // Each row previews its own face; a file that no longer loads just
    // falls back to the app's mono, name-only
    val fontFamilies = remember(importedFonts) {
        importedFonts.associate { file ->
            file.name to FontInfo.family(file)
        }
    }
    // A selection pointing at a deleted file behaves as the bundled default
    val effectiveSelection = if (importedFonts.any { it.name == selected }) selected else ""

    val fontPickerLauncher = rememberLauncherForActivityResult(
        // Unfiltered like the tarball picker: font MIME types are unreliable
        // across file managers, the real validation is Typeface.createFromFile
        contract = ActivityResultContracts.GetContent()
    ) { uri ->
        if (uri == null) return@rememberLauncherForActivityResult
        scope.launch {
            importError = null
            val fileName = FilePickerUtils.getFileName(context, uri)
            val safeName = fileName
                ?.takeIf { it.substringAfterLast('.').lowercase() in FONT_EXTENSIONS }
                ?.let { FontInfo.safeFileName(it) }
            if (safeName == null) {
                importError = context.getString(R.string.terminal_font_import_error, fileName ?: uri.toString())
                return@launch
            }
            // ponytail: an existing filename is overwritten, re-importing IS the update path
            val dest = File(FontInfo.fontsDir(context).apply { mkdirs() }, safeName)
            val loaded = withContext(Dispatchers.IO) {
                runCatching {
                    context.contentResolver.openInputStream(uri)?.use { input ->
                        dest.outputStream().use { input.copyTo(it) }
                    } ?: error("unreadable uri")
                    Typeface.createFromFile(dest) // throws if the bytes are not a font
                }.isSuccess
            }
            if (loaded) {
                selected = safeName
                importedFonts = listFonts()
            } else {
                dest.delete()
                importError = context.getString(R.string.terminal_font_import_error, fileName)
            }
        }
    }

    DsDialog(
        onDismiss = onDismiss,
        footer = {
            DialogFooterRow(
                dismissLabel = context.getString(R.string.cancel),
                confirmLabel = context.getString(R.string.ok),
                onDismiss = onDismiss,
                onConfirm = { onConfirm(effectiveSelection) },
            )
        }
    ) {
        Column {
            Text(
                context.getString(R.string.terminal_font_family),
                style = MaterialTheme.typography.titleLarge,
                fontWeight = FontWeight.Bold
            )
            Text(
                context.getString(R.string.terminal_font_description),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f),
                modifier = Modifier.padding(top = 2.dp)
            )
        }

        Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
            DsRadioRow(
                label = context.getString(R.string.terminal_font_default),
                fontFamily = JetBrainsMono,
                selected = effectiveSelection.isEmpty(),
                onClick = { selected = "" }
            )
            importedFonts.forEach { file ->
                DsRadioRow(
                    label = fontNames[file.name] ?: file.nameWithoutExtension,
                    fontFamily = fontFamilies[file.name] ?: JetBrainsMono,
                    selected = effectiveSelection == file.name,
                    onClick = { selected = file.name },
                    delete = context.getString(R.string.terminal_font_delete) to {
                        file.delete()
                        if (selected == file.name) selected = ""
                        importedFonts = listFonts()
                    }
                )
            }
        }

        importError?.let {
            Text(
                text = it,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.error
            )
        }

        AddEntryButton(context.getString(R.string.terminal_font_import), onClick = { fontPickerLauncher.launch("*/*") })
    }
}
