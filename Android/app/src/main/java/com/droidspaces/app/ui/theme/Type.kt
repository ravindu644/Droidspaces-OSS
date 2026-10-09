package com.droidspaces.app.ui.theme

import androidx.compose.material3.Typography
import androidx.compose.ui.text.ExperimentalTextApi
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontVariation
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp
import com.droidspaces.app.R

/*
 * roboto_flex.ttf is Roboto Flex instanced down to its wght axis (400 to 700), which is
 * all the scale below uses, so it ships at about 150 KB instead of 1.7 MB. Each weight
 * sets the axis explicitly: without the variation setting Android renders the default
 * instance and fakes bold on top of it.
 */
@OptIn(ExperimentalTextApi::class)
val RobotoFlex = FontFamily(
    listOf(400, 500, 600, 700).map {
        Font(
            R.font.roboto_flex,
            FontWeight(it),
            variationSettings = FontVariation.Settings(FontVariation.weight(it))
        )
    }
)

private fun style(size: Int, line: Int, weight: FontWeight, tracking: Double = 0.0) = TextStyle(
    fontFamily = RobotoFlex,
    fontWeight = weight,
    fontSize = size.sp,
    lineHeight = line.sp,
    letterSpacing = tracking.sp
)

/*
 * The full constructor, so every Emphasized slot is a real style. The 15-slot one
 * aliases them to the base slots and Emphasized text would silently look regular.
 * Sizes and weights are the DESIGN.md typography table; an Emphasized slot not named
 * there is its base slot one weight step up.
 */
val Typography = Typography(
    displayLarge = style(57, 64, FontWeight.Bold, -0.25),
    displayMedium = style(45, 52, FontWeight.Bold),
    displaySmall = style(36, 44, FontWeight.Bold),
    headlineLarge = style(32, 40, FontWeight.SemiBold),
    headlineMedium = style(28, 36, FontWeight.SemiBold),
    headlineSmall = style(24, 30, FontWeight.SemiBold),
    titleLarge = style(22, 28, FontWeight.SemiBold),
    titleMedium = style(16, 24, FontWeight.Medium, 0.15),
    titleSmall = style(14, 20, FontWeight.Medium, 0.1),
    bodyLarge = style(16, 22, FontWeight.Normal, 0.5),
    bodyMedium = style(14, 20, FontWeight.Normal, 0.25),
    bodySmall = style(13, 18, FontWeight.Normal, 0.4),
    labelLarge = style(14, 20, FontWeight.SemiBold, 0.1),
    labelMedium = style(12, 16, FontWeight.SemiBold, 0.5),
    labelSmall = style(11, 16, FontWeight.Medium, 0.5),
    displayLargeEmphasized = style(57, 64, FontWeight.Bold, -0.25),
    displayMediumEmphasized = style(45, 52, FontWeight.Bold),
    displaySmallEmphasized = style(36, 40, FontWeight.Bold),
    headlineLargeEmphasized = style(32, 40, FontWeight.Bold),
    headlineMediumEmphasized = style(28, 36, FontWeight.Bold),
    headlineSmallEmphasized = style(24, 30, FontWeight.Bold),
    titleLargeEmphasized = style(20, 26, FontWeight.Bold),
    titleMediumEmphasized = style(18, 24, FontWeight.SemiBold),
    titleSmallEmphasized = style(14, 20, FontWeight.SemiBold, 0.1),
    bodyLargeEmphasized = style(16, 22, FontWeight.Medium, 0.5),
    bodyMediumEmphasized = style(14, 20, FontWeight.Medium, 0.25),
    bodySmallEmphasized = style(13, 18, FontWeight.Medium, 0.4),
    labelLargeEmphasized = style(14, 20, FontWeight.Bold, 0.1),
    labelMediumEmphasized = style(12, 16, FontWeight.Bold, 0.5),
    labelSmallEmphasized = style(11, 16, FontWeight.SemiBold, 0.5)
)

val JetBrainsMono = FontFamily(
    Font(R.font.jetbrains_mono_regular, FontWeight.Normal),
    Font(R.font.jetbrains_mono_bold, FontWeight.Bold),
    Font(R.font.jetbrains_mono_italic, FontWeight.Normal, FontStyle.Italic),
    Font(R.font.jetbrains_mono_bold_italic, FontWeight.Bold, FontStyle.Italic)
)
