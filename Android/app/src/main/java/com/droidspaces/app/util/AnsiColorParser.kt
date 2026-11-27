package com.droidspaces.app.util

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextDecoration

/** SGR colour and attribute codes in a log line, as Compose spans. */
object AnsiColorParser {
    // Foreground codes 30-37 and 90-97; a background code is the same colour at +10.
    private val ansiColors = mapOf(
        30 to Color(0xFF000000), 31 to Color(0xFFCD3131), 32 to Color(0xFF0DBC79), 33 to Color(0xFFE5E510),
        34 to Color(0xFF2472C8), 35 to Color(0xFFBC3FBC), 36 to Color(0xFF11A8CD), 37 to Color(0xFFE5E5E5),
        90 to Color(0xFF767676), 91 to Color(0xFFFF6B6B), 92 to Color(0xFF51CF66), 93 to Color(0xFFFFD93D),
        94 to Color(0xFF74C0FC), 95 to Color(0xFFFF8CC8), 96 to Color(0xFF66D9EF), 97 to Color(0xFFFFFFFF),
    )
    private val sgr = Regex("""\u001B\[([0-9;]*)m""")

    fun parseAnsi(text: String, defaultColor: Color): AnnotatedString {
        if (!text.contains("\u001B[")) return AnnotatedString(text)
        return buildAnnotatedString {
            var fg = 0
            var bg = 0
            var bold = false
            var dim = false
            var italic = false
            var underline = false
            var from = 0

            fun flush(upTo: Int) {
                if (upTo <= from) return
                val start = length
                append(text, from, upTo)
                // Bold on a standard colour draws its bright variant, as terminals render "1;32".
                val color = ansiColors[if (bold && fg in 30..37) fg + 60 else fg] ?: defaultColor
                addStyle(
                    SpanStyle(
                        color = if (dim) color.copy(alpha = 0.6f) else color,
                        background = ansiColors[bg - 10] ?: Color.Unspecified,
                        fontWeight = if (bold) FontWeight.Bold else FontWeight.Normal,
                        fontStyle = if (italic) FontStyle.Italic else FontStyle.Normal,
                        textDecoration = if (underline) TextDecoration.Underline else null,
                    ),
                    start, length,
                )
            }

            for (match in sgr.findAll(text)) {
                flush(match.range.first)
                // An empty parameter is 0: a bare ESC[m, and the gap in ESC[;31m, both reset.
                for (code in match.groupValues[1].split(';').map { it.toIntOrNull() ?: 0 }) {
                    when (code) {
                        0 -> { fg = 0; bg = 0; bold = false; dim = false; italic = false; underline = false }
                        1 -> bold = true
                        2 -> dim = true
                        3 -> italic = true
                        4 -> underline = true
                        22 -> { bold = false; dim = false }
                        23 -> italic = false
                        24 -> underline = false
                        in 30..37, in 90..97 -> fg = code
                        39 -> fg = 0
                        in 40..47, in 100..107 -> bg = code
                        49 -> bg = 0
                    }
                }
                from = match.range.last + 1
            }
            flush(text.length)
        }
    }

    /** The plain text, for the clipboard. */
    fun stripAnsi(text: String): String = text.replace(sgr, "")
}
