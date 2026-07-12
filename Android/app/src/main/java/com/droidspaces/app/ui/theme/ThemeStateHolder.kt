package com.droidspaces.app.ui.theme

import android.content.SharedPreferences
import android.os.Build
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import com.droidspaces.app.util.PreferencesManager

/**
 * State holder for theme preferences that updates reactively.
 * This allows instant theme changes without app restart.
 *
 * Uses SharedPreferences.OnSharedPreferenceChangeListener to detect
 * preference changes and update state reactively.
 * Based on KernelSU-Next's theme system.
 */
@Composable
fun rememberThemeState(): ThemeState {
    val context = LocalContext.current
    val prefsManager = remember { PreferencesManager.getInstance(context) }

    // Use mutableStateOf to hold theme state - this triggers recomposition when changed
    var followSystemTheme by remember { mutableStateOf(prefsManager.followSystemTheme) }
    var darkTheme by remember { mutableStateOf(prefsManager.darkTheme) }
    var amoledMode by remember { mutableStateOf(prefsManager.amoledMode) }
    var useDynamicColor by remember {
        mutableStateOf(
            // Only allow dynamic color on Android 12+
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                prefsManager.useDynamicColor
            } else {
                false
            }
        )
    }
    var themePalette by remember {
        mutableStateOf(ThemePalette.fromName(prefsManager.themePalette))
    }

    // Register SharedPreferences listener to detect changes
    DisposableEffect(prefsManager) {
        val listener = SharedPreferences.OnSharedPreferenceChangeListener { _, key ->
            // Update state when theme-related preferences change
            when (key) {
                "follow_system_theme" -> followSystemTheme = prefsManager.followSystemTheme
                "dark_theme" -> darkTheme = prefsManager.darkTheme
                "amoled_mode" -> amoledMode = prefsManager.amoledMode
                "use_dynamic_color" -> {
                    // Only allow dynamic color on Android 12+
                    useDynamicColor = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                        prefsManager.useDynamicColor
                    } else {
                        false
                    }
                }
                "theme_palette" -> {
                    themePalette = ThemePalette.fromName(prefsManager.themePalette)
                }
            }
        }

        // Get the underlying SharedPreferences and register listener
        prefsManager.prefs.registerOnSharedPreferenceChangeListener(listener)

        // Cleanup: unregister listener when composable is disposed
        onDispose {
            prefsManager.prefs.unregisterOnSharedPreferenceChangeListener(listener)
        }
    }

    val systemDark = isSystemInDarkTheme()
    val effectiveDarkTheme = if (followSystemTheme) systemDark else darkTheme

    // Return state that triggers recomposition when any theme preference changes
    return remember(followSystemTheme, darkTheme, amoledMode, useDynamicColor, systemDark, themePalette) {
        ThemeState(
            followSystemTheme = followSystemTheme,
            darkTheme = effectiveDarkTheme,
            amoledMode = amoledMode,
            useDynamicColor = useDynamicColor,
            themePalette = themePalette
        )
    }
}

data class ThemeState(
    val followSystemTheme: Boolean,
    val darkTheme: Boolean,
    val amoledMode: Boolean,
    val useDynamicColor: Boolean,
    val themePalette: ThemePalette
)
