package com.droidspaces.app.util

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.material3.MaterialTheme

/**
 * High-performance ANSI color parser optimized for real-time terminal rendering.
 * Pre-computes color mappings and uses efficient regex patterns for minimal overhead.
 */
object AnsiColorParser {
    // Pre-computed ANSI color mappings (8 standard colors + bright variants)
    private val ansiColors = mapOf(
        // Standard colors
        30 to Color(0xFF000000), // Black
        31 to Color(0xFFCD3131), // Red
        32 to Color(0xFF0DBC79), // Green
        33 to Color(0xFFE5E510), // Yellow
        34 to Color(0xFF2472C8), // Blue
        35 to Color(0xFFBC3FBC), // Magenta
        36 to Color(0xFF11A8CD), // Cyan
        37 to Color(0xFFE5E5E5), // White

        // Bright colors (90-97)
        90 to Color(0xFF767676), // Bright Black (Gray)
        91 to Color(0xFFFF6B6B), // Bright Red
        92 to Color(0xFF51CF66), // Bright Green
        93 to Color(0xFFFFD93D), // Bright Yellow
        94 to Color(0xFF74C0FC), // Bright Blue
        95 to Color(0xFFFF8CC8), // Bright Magenta
        96 to Color(0xFF66D9EF), // Bright Cyan
        97 to Color(0xFFFFFFFF), // Bright White
    )

    // Background colors (40-47, 100-107)
    private val ansiBgColors = mapOf(
        40 to Color(0xFF000000),
        41 to Color(0xFFCD3131),
        42 to Color(0xFF0DBC79),
        43 to Color(0xFFE5E510),
        44 to Color(0xFF2472C8),
        45 to Color(0xFFBC3FBC),
        46 to Color(0xFF11A8CD),
        47 to Color(0xFFE5E5E5),
        100 to Color(0xFF767676),
        101 to Color(0xFFFF6B6B),
        102 to Color(0xFF51CF66),
        103 to Color(0xFFFFD93D),
        104 to Color(0xFF74C0FC),
        105 to Color(0xFFFF8CC8),
        106 to Color(0xFF66D9EF),
        107 to Color(0xFFFFFFFF),
    )

    /**
     * Parse ANSI escape codes and return AnnotatedString with color spans.
     * Optimized for performance with minimal allocations.
     */
    fun parseAnsi(text: String, defaultColor: Color): AnnotatedString {
        if (!text.contains("\u001B[")) {
            // Fast path: no ANSI codes, return plain text
            return AnnotatedString(text)
        }

        return buildAnnotatedString {
            var currentColor: Color? = null
            var currentBgColor: Color? = null
            var isBold = false
            var isDim = false
            var isItalic = false
            var isUnderline = false
            var currentIndex = 0

            // Regex pattern for ANSI escape sequences: \u001B[<codes>m
            val ansiPattern = Regex("""\u001B\[([0-9;]*)m""")
            val matches = ansiPattern.findAll(text)

            for (match in matches) {
                // Add text before the ANSI code
                if (match.range.first > currentIndex) {
                    val textSegment = text.substring(currentIndex, match.range.first)
                    if (textSegment.isNotEmpty()) {
                        val start = length
                        append(textSegment)
                        val end = length
                        addStyle(
                            createSpanStyle(currentColor ?: defaultColor, currentBgColor, isBold, isDim, isItalic, isUnderline),
                            start,
                            end
                        )
                    }
                }

                // Parse ANSI codes
                val codes = match.groupValues[1]
                if (codes.isEmpty()) {
                    // Reset all styles
                    currentColor = null
                    currentBgColor = null
                    isBold = false
                    isDim = false
                    isItalic = false
                    isUnderline = false
                } else {
                    val codeList = codes.split(';').mapNotNull { it.toIntOrNull() }
                    for (code in codeList) {
                        when (code) {
                            0 -> {
                                // Reset all
                                currentColor = null
                                currentBgColor = null
                                isBold = false
                                isDim = false
                                isItalic = false
                                isUnderline = false
                            }
                            1 -> isBold = true
                            2 -> isDim = true
                            3 -> isItalic = true
                            4 -> isUnderline = true
                            22 -> {
                                isBold = false
                                isDim = false
                            }
                            23 -> isItalic = false
                            24 -> isUnderline = false
                            in 30..37 -> currentColor = ansiColors[code]
                            in 90..97 -> currentColor = ansiColors[code]
                            39 -> currentColor = null
                            in 40..47 -> currentBgColor = ansiBgColors[code]
                            in 100..107 -> currentBgColor = ansiBgColors[code]
                            49 -> currentBgColor = null
                        }
                    }

                    // Apply bold modifier to colors if both bold and color are set
                    // This handles compound codes like "1;32m" (bold + green)
                    if (isBold && currentColor != null) {
                        // Use bright color variants when bold is applied to standard colors
                        currentColor = when (currentColor) {
                            ansiColors[30] -> ansiColors[90]  // Black -> Bright Black (Gray)
                            ansiColors[31] -> ansiColors[91]  // Red -> Bright Red
                            ansiColors[32] -> ansiColors[92]  // Green -> Bright Green
                            ansiColors[33] -> ansiColors[93]  // Yellow -> Bright Yellow
                            ansiColors[34] -> ansiColors[94]  // Blue -> Bright Blue
                            ansiColors[35] -> ansiColors[95]  // Magenta -> Bright Magenta
                            ansiColors[36] -> ansiColors[96]  // Cyan -> Bright Cyan
                            ansiColors[37] -> ansiColors[97]  // White -> Bright White
                            else -> currentColor
                        }
                    }
                }

                currentIndex = match.range.last + 1
            }

            // Add remaining text
            if (currentIndex < text.length) {
                val textSegment = text.substring(currentIndex)
                if (textSegment.isNotEmpty()) {
                    val start = length
                    append(textSegment)
                    val end = length
                    addStyle(
                        createSpanStyle(currentColor ?: defaultColor, currentBgColor, isBold, isDim, isItalic, isUnderline),
                        start,
                        end
                    )
                }
            }
        }
    }

    private fun createSpanStyle(
        color: Color,
        bgColor: Color?,
        bold: Boolean,
        dim: Boolean,
        italic: Boolean,
        underline: Boolean
    ): SpanStyle {
        return SpanStyle(
            color = if (dim) color.copy(alpha = 0.6f) else color,
            background = bgColor ?: Color.Unspecified,
            fontWeight = if (bold) FontWeight.Bold else FontWeight.Normal,
            fontStyle = if (italic) androidx.compose.ui.text.font.FontStyle.Italic else androidx.compose.ui.text.font.FontStyle.Normal,
            textDecoration = if (underline) androidx.compose.ui.text.style.TextDecoration.Underline else null
        )
    }

    /**
     * Strip ANSI codes from text (for clipboard/copy operations).
     */
    fun stripAnsi(text: String): String {
        return text.replace(Regex("""\u001B\[([0-9;]*)m"""), "")
    }
}

