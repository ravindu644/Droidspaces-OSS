package com.droidspaces.app.ui.theme

import android.os.Build
import androidx.activity.ComponentActivity
import androidx.activity.SystemBarStyle
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.runtime.SideEffect
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalContext

/**
 * Blend two colors by the given ratio.
 * ratio=0 → returns this, ratio=1 → returns other.
 */
@Suppress("NOTHING_TO_INLINE")
private inline fun Color.blend(other: Color, ratio: Float): Color {
    val inv = 1f - ratio
    return Color(
        red = red * inv + other.red * ratio,
        green = green * inv + other.green * ratio,
        blue = blue * inv + other.blue * ratio,
        alpha = 1f
    )
}

/**
 * Create a complete dark color scheme from the given [ThemePalette].
 * Derives tinted surfaces, containers, and on-colors from the palette primaries
 * to mimic Android's Monet-style full-scheme generation.
 */
private fun darkColorSchemeFor(palette: ThemePalette): ColorScheme {
    val p = palette.primaryDark
    val s = palette.secondaryDark
    val t = palette.tertiaryDark
    val base = Color(0xFF121212) // M3 dark baseline

    return darkColorScheme(
        primary = p,
        onPrimary = p.blend(Color.Black, 0.70f),
        primaryContainer = p.blend(Color.Black, 0.40f),
        onPrimaryContainer = p.blend(Color.White, 0.75f),

        secondary = s,
        onSecondary = s.blend(Color.Black, 0.70f),
        secondaryContainer = s.blend(Color.Black, 0.40f),
        onSecondaryContainer = s.blend(Color.White, 0.75f),

        tertiary = t,
        onTertiary = t.blend(Color.Black, 0.70f),
        tertiaryContainer = t.blend(Color.Black, 0.40f),
        onTertiaryContainer = t.blend(Color.White, 0.75f),

        background = base.blend(p, 0.15f), // Heavier tint for "soul"
        onBackground = Color(0xFFE2E2E6),
        surface = base.blend(p, 0.15f),
        onSurface = Color(0xFFE2E2E6),
        surfaceVariant = Color(0xFF2B2B2F).blend(p, 0.25f), // More vibrant variant
        onSurfaceVariant = Color(0xFFC6C6CA),

        surfaceContainer = Color(0xFF1E1E22).blend(p, 0.20f),
        surfaceContainerHigh = Color(0xFF282830).blend(p, 0.22f),
        surfaceContainerHighest = Color(0xFF333338).blend(p, 0.25f),
        surfaceContainerLow = Color(0xFF1A1A1E).blend(p, 0.18f),
        surfaceContainerLowest = Color(0xFF0F0F13).blend(p, 0.15f),

        outline = Color(0xFF8E8E93).blend(p, 0.35f),
        outlineVariant = Color(0xFF46464A).blend(p, 0.30f),
        inverseSurface = Color(0xFFE2E2E6),
        inverseOnSurface = Color(0xFF303034),
        inversePrimary = palette.primaryLight
    )
}

/**
 * Create a complete light color scheme from the given [ThemePalette].
 * Derives tinted surfaces, containers, and on-colors from the palette primaries
 * to mimic Android's Monet-style full-scheme generation.
 */
private fun lightColorSchemeFor(palette: ThemePalette): ColorScheme {
    val p = palette.primaryLight
    val s = palette.secondaryLight
    val t = palette.tertiaryLight
    val base = Color(0xFFFFFBFF) // M3 light baseline

    return lightColorScheme(
        primary = p,
        onPrimary = p.blend(Color.White, 0.85f),
        primaryContainer = p.blend(Color.White, 0.55f),
        onPrimaryContainer = p.blend(Color.Black, 0.55f),

        secondary = s,
        onSecondary = s.blend(Color.White, 0.85f),
        secondaryContainer = s.blend(Color.White, 0.55f),
        onSecondaryContainer = s.blend(Color.Black, 0.55f),

        tertiary = t,
        onTertiary = t.blend(Color.White, 0.85f),
        tertiaryContainer = t.blend(Color.White, 0.55f),
        onTertiaryContainer = t.blend(Color.Black, 0.55f),

        background = base.blend(p, 0.06f),
        onBackground = Color(0xFF1B1B1F),
        surface = base.blend(p, 0.06f),
        onSurface = Color(0xFF1B1B1F),
        surfaceVariant = Color(0xFFE4E1E6).blend(p, 0.15f),
        onSurfaceVariant = Color(0xFF46464A),

        surfaceContainer = Color(0xFFF0EDF1).blend(p, 0.10f),
        surfaceContainerHigh = Color(0xFFEAE7EB).blend(p, 0.12f),
        surfaceContainerHighest = Color(0xFFE4E1E6).blend(p, 0.14f),
        surfaceContainerLow = Color(0xFFF6F3F7).blend(p, 0.08f),
        surfaceContainerLowest = base.blend(p, 0.04f),

        outline = Color(0xFF767680).blend(p, 0.22f),
        outlineVariant = Color(0xFFC6C6CA).blend(p, 0.18f),
        inverseSurface = Color(0xFF303034),
        inverseOnSurface = Color(0xFFF2F0F4),
        inversePrimary = palette.primaryDark
    )
}

/**
 * AMOLED collapses every surface to true black, so the accent has to carry the
 * hierarchy on its own. Containers are blended down hard to stay readable on it.
 */
private fun amoledSchemeFrom(dynamicScheme: ColorScheme): ColorScheme =
    dynamicScheme.copy(
        background = AMOLED_BLACK,
        surface = AMOLED_BLACK,
        surfaceVariant = AMOLED_BLACK,
        surfaceContainer = AMOLED_BLACK,
        surfaceContainerLow = AMOLED_BLACK,
        surfaceContainerLowest = AMOLED_BLACK,
        surfaceContainerHigh = AMOLED_BLACK,
        surfaceContainerHighest = AMOLED_BLACK,
        primaryContainer = dynamicScheme.primaryContainer.blend(AMOLED_BLACK, 0.7f),
        secondaryContainer = dynamicScheme.secondaryContainer.blend(AMOLED_BLACK, 0.7f),
        tertiaryContainer = dynamicScheme.tertiaryContainer.blend(AMOLED_BLACK, 0.7f)
    )

/**
 * Same idea for the fixed palettes, but the surfaces keep a trace of the selected
 * accent so the palette is still recognisable against the black.
 */
private fun staticAmoledSchemeFor(palette: ThemePalette): ColorScheme {
    val p = palette.primaryDark
    return darkColorSchemeFor(palette).copy(
        background = AMOLED_BLACK,
        surface = AMOLED_BLACK,
        surfaceVariant = AMOLED_BLACK,
        surfaceContainer = AMOLED_BLACK,
        surfaceContainerLow = AMOLED_BLACK,
        surfaceContainerLowest = AMOLED_BLACK,
        surfaceContainerHigh = AMOLED_BLACK,
        surfaceContainerHighest = AMOLED_BLACK,
        outlineVariant = p.copy(alpha = 0.25f), // Keep subtle outline for hierarchy
        // primaryContainer keeps the dark scheme's opaque blend: a translucent accent here
        // let the list show through the FAB menu, and no other container role has alpha
        onPrimaryContainer = p.blend(Color.White, 0.85f)
    )
}

@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
fun DroidspacesTheme(
    darkTheme: Boolean = isSystemInDarkTheme(),
    // Dynamic color is available on Android 12+
    dynamicColor: Boolean = true,
    amoledMode: Boolean = false,
    themePalette: ThemePalette = ThemePalette.CATPPUCCIN,
    content: @Composable () -> Unit
) {
    val context = LocalContext.current

    // Memoize color scheme computation to avoid recalculation on every recomposition
    // This eliminates color processing from the main thread during draw cycles
    val colorScheme = remember(darkTheme, dynamicColor, amoledMode, themePalette, context) {
        when {
        amoledMode && darkTheme && dynamicColor && Build.VERSION.SDK_INT >= Build.VERSION_CODES.S -> {
                // Pre-computed AMOLED scheme with dynamic colors - zero runtime cost
            val dynamicScheme = dynamicDarkColorScheme(context)
                amoledSchemeFrom(dynamicScheme)
        }
        dynamicColor && Build.VERSION.SDK_INT >= Build.VERSION_CODES.S -> {
                // Memoized dynamic color scheme - computed once per theme change
            if (darkTheme) dynamicDarkColorScheme(context) else dynamicLightColorScheme(context)
        }
        amoledMode && darkTheme -> {
                // Pre-computed static AMOLED scheme using selected palette
                staticAmoledSchemeFor(themePalette)
        }
        darkTheme -> darkColorSchemeFor(themePalette)
        else -> lightColorSchemeFor(themePalette)
        }
    }

    SystemBarStyle(
        darkMode = darkTheme
    )

    MaterialExpressiveTheme(
        colorScheme = colorScheme,
        motionScheme = MotionScheme.expressive(),
        shapes = Shapes,
        typography = Typography,
        content = content
    )
}

@Composable
private fun SystemBarStyle(
    darkMode: Boolean,
    statusBarScrim: Color = Color.Transparent,
    navigationBarScrim: Color = Color.Transparent,
) {
    val context = LocalContext.current
    val activity = context as? ComponentActivity

    SideEffect {
        activity?.enableEdgeToEdge(
            statusBarStyle = SystemBarStyle.auto(
                statusBarScrim.toArgb(),
                statusBarScrim.toArgb(),
            ) { darkMode },
            navigationBarStyle = when {
                darkMode -> SystemBarStyle.dark(
                    navigationBarScrim.toArgb()
                )

                else -> SystemBarStyle.light(
                    navigationBarScrim.toArgb(),
                    navigationBarScrim.toArgb(),
                )
            }
        )
    }
}
