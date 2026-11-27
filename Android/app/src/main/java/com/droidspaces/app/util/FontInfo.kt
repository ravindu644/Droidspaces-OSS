package com.droidspaces.app.util

import android.content.Context
import android.graphics.Typeface
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.Typeface as ComposeTypeface
import java.io.File
import java.io.RandomAccessFile

/**
 * Imported terminal fonts live as flat files in filesDir/fonts/ and the
 * preference stores just the filename, so the picker and the terminal only
 * need to agree on this directory.
 */
object FontInfo {

    fun fontsDir(context: Context): File = File(context.filesDir, "fonts")

    // DISPLAY_NAME comes from an arbitrary DocumentsProvider, so it can carry
    // path separators or worse. Reduce it to a flat, safe filename before it
    // ever touches a File() path.
    fun safeFileName(raw: String): String? =
        raw.substringAfterLast('/').substringAfterLast('\\')
            .replace(Regex("[^A-Za-z0-9 ._()-]"), "_")
            .trim()
            .takeIf { it.isNotEmpty() && it != "." && it != ".." }

    /**
     * The font's own name from its sfnt `name` table, because "Adwaita Mono"
     * reads better in a picker than "AdwaitaMono-Regular.ttf". Android has no
     * public API for this, so it is a ~40 line binary parse; any failure falls
     * back to the filename.
     */
    fun displayName(file: File): String =
        runCatching { parseName(file) }.getOrNull() ?: file.nameWithoutExtension

    /** Null when the file no longer loads, so callers fall back to the bundled face. */
    fun family(file: File): FontFamily? =
        runCatching { FontFamily(ComposeTypeface(Typeface.createFromFile(file))) }.getOrNull()

    private fun parseName(file: File): String? = RandomAccessFile(file, "r").use { f ->
        // 'ttcf' collections keep the first face's offset table pointer at byte 12
        val face = if (f.readU32(0) == 0x74746366L) f.readU32(12) else 0L
        val tag = f.readU32(face)
        if (tag != 0x00010000L && tag != 0x4F54544FL) return null // not TTF, not 'OTTO'

        val numTables = f.readU16(face + 4)
        var nameTable = -1L
        for (i in 0 until numTables) {
            val rec = face + 12 + i * 16L
            if (f.readU32(rec) == 0x6E616D65L) { // 'name'
                nameTable = f.readU32(rec + 8)
                break
            }
        }
        if (nameTable < 0) return null

        val count = f.readU16(nameTable + 2)
        val stringsStart = nameTable + f.readU16(nameTable + 4)
        var best: String? = null
        var bestRank = Int.MAX_VALUE
        for (i in 0 until count) {
            val rec = nameTable + 6 + i * 12L
            val platform = f.readU16(rec)
            val encoding = f.readU16(rec + 2)
            // nameID 4 = full name, 1 = family; Windows/Unicode records first,
            // old Mac records as a fallback
            val rank = when {
                platform == 3 && encoding == 1 && f.readU16(rec + 6) == 4 -> 0
                platform == 3 && encoding == 1 && f.readU16(rec + 6) == 1 -> 1
                platform == 1 && f.readU16(rec + 6) == 4 -> 2
                platform == 1 && f.readU16(rec + 6) == 1 -> 3
                else -> continue
            }
            if (rank >= bestRank) continue
            val bytes = ByteArray(f.readU16(rec + 8))
            val strOffset = f.readU16(rec + 10)
            f.seek(stringsStart + strOffset)
            f.readFully(bytes)
            val charset = if (platform == 3) Charsets.UTF_16BE else Charsets.ISO_8859_1
            String(bytes, charset).trim().takeIf { it.isNotEmpty() }?.let {
                best = it
                bestRank = rank
            }
        }
        best
    }

    private fun RandomAccessFile.readU16(at: Long): Int {
        seek(at)
        return readUnsignedShort()
    }

    private fun RandomAccessFile.readU32(at: Long): Long {
        seek(at)
        return readInt().toLong() and 0xFFFFFFFFL
    }
}
