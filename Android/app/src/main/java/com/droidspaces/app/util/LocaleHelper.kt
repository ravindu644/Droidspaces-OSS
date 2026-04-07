package com.droidspaces.app.util

import android.content.Context
import android.content.res.Configuration
import androidx.appcompat.app.AppCompatDelegate
import androidx.core.os.LocaleListCompat
import java.util.Locale

/**
 * Modern LocaleHelper using AppCompatDelegate.setApplicationLocales() API.
 *
 * This implementation works on ALL Android versions (API 24+) and ALL devices
 * including Samsung One UI 8+. It uses the modern AppCompat API which:
 * - Automatically saves locale preferences (with autoStoreLocales=true)
 * - Automatically recreates activities
 * - Updates all string resources
 * - Works identically across all Android versions
 *
 * The list of supported languages is driven entirely by the build-time generated
 * file assets/supported_locales.txt (written by the generateSupportedLocalesList
 * Gradle task). Adding a new translation via Weblate automatically makes it
 * appear in the language picker on the next build — no code changes needed.
 */
object LocaleHelper {

    /**
     * Read locale codes from the build-generated assets/supported_locales.txt.
     * Falls back to an empty list if the file is missing (shouldn't happen in
     * a proper build, but guards against edge cases like running from IDE
     * before the Gradle task has executed).
     */
    private fun readSupportedLocaleCodes(context: Context): List<String> {
        return try {
            context.assets.open("supported_locales.txt")
                .bufferedReader()
                .readLines()
                .map { it.trim() }
                .filter { it.isNotEmpty() }
        } catch (_: Exception) {
            emptyList()
        }
    }

    /**
     * Build a Language object from an Android resource qualifier locale code
     * (e.g. "pt-rBR", "zh-rCN", "si", "tr").
     *
     * Display names are derived automatically via the JVM Locale API so no
     * manual name strings are needed when new languages are added.
     */
    private fun localeCodeToLanguage(code: String): Language {
        // Convert Android qualifier format (e.g. pt-rBR) → BCP 47 (pt-BR)
        val bcp47 = code.replace("-r", "-")
        val locale = Locale.forLanguageTag(bcp47)

        val englishName = if (locale.country.isNotEmpty()) {
            "${locale.getDisplayLanguage(Locale.ENGLISH)} (${locale.getDisplayCountry(Locale.ENGLISH)})"
        } else {
            locale.getDisplayLanguage(Locale.ENGLISH)
        }.replaceFirstChar { it.uppercaseChar() }

        val nativeName = if (locale.country.isNotEmpty()) {
            "${locale.getDisplayLanguage(locale)} (${locale.getDisplayCountry(locale)})"
        } else {
            locale.getDisplayLanguage(locale)
        }.replaceFirstChar { it.uppercaseChar() }

        return Language(
            code = code,
            displayName = englishName.ifBlank { code },
            nativeName = nativeName.ifBlank { code }
        )
    }

    /**
     * Get available languages for the in-app language picker.
     *
     * The list is built dynamically from assets/supported_locales.txt which is
     * generated at build time by scanning the project's res/values-* directories.
     * It is sorted alphabetically by English display name.
     *
     * English is always included as the first entry (it is the app's default /
     * fallback language and does not need a values-en directory).
     */
    fun getAvailableLanguages(context: Context): List<Language> {
        val codes = readSupportedLocaleCodes(context)

        val languages = mutableListOf<Language>()
        // English is the default; no values-en directory exists, so add it explicitly.
        languages.add(Language("en", "English", "English"))

        codes
            .filter { it != "en" } // avoid duplicating English if someone adds values-en
            .map { localeCodeToLanguage(it) }
            .sortedBy { it.displayName }
            .forEach { languages.add(it) }

        return languages
    }

    /**
     * Get current language code in the format used by Language.code.
     * Returns language code (e.g., "en", "es", "pt-rBR") or "system" for system default.
     */
    fun getCurrentLanguageCode(): String {
        val locales = AppCompatDelegate.getApplicationLocales()
        if (locales.isEmpty) {
            return "system"
        }

        val locale = locales.get(0) ?: return "system"

        // Format: "language" or "language-rREGION" to match Language.code format
        return if (locale.country.isNotEmpty()) {
            "${locale.language}-r${locale.country}"
        } else {
            locale.language
        }
    }

    /**
     * Get current language code (legacy method name).
     * @deprecated Use getCurrentLanguageCode() instead
     */
    @Deprecated("Use getCurrentLanguageCode() instead", ReplaceWith("getCurrentLanguageCode()"))
    fun getCurrentLanguage(): String {
        return getCurrentLanguageCode()
    }

    /**
     * Get current app locale for display purposes.
     * Returns Locale object or null for system default.
     */
    @Suppress("UNUSED_PARAMETER")
    fun getCurrentAppLocale(context: Context): Locale? {
        val locales = AppCompatDelegate.getApplicationLocales()
        return if (locales.isEmpty) {
            null // System default
        } else {
            locales.get(0)
        }
    }

    /**
     * Change application language.
     *
     * This is the critical function that uses AppCompatDelegate.setApplicationLocales().
     * It automatically:
     * - Saves the locale preference (with autoStoreLocales=true)
     * - Recreates the activity
     * - Updates all string resources
     * - Works on ALL Android versions (API 24+)
     *
     * @param languageCode Language code (e.g., "en", "es", "pt-rBR") or "system" for system default
     */
    fun changeLanguage(languageCode: String) {
        // Get current language code to avoid unnecessary recreation
        val currentCode = getCurrentLanguageCode()

        // Only change if different
        if (currentCode != languageCode) {
            val appLocale = if (languageCode == "system") {
                LocaleListCompat.getEmptyLocaleList()
            } else {
                // Convert our format (e.g., "pt-rBR") to BCP 47 format (e.g., "pt-BR")
                val bcp47Tag = languageCode.replace("-r", "-")
                LocaleListCompat.forLanguageTags(bcp47Tag)
            }

            // This automatically:
            // - Saves the locale preference (with autoStoreLocales=true)
            // - Recreates the activity
            // - Updates all string resources
            // - Works on ALL Android versions (API 24+)
            AppCompatDelegate.setApplicationLocales(appLocale)
        }
    }

    /**
     * Get supported locales as Locale objects.
     * Returns list including Locale.ROOT first (represents "System Default").
     * Built dynamically from assets/supported_locales.txt.
     */
    fun getSupportedLocales(context: Context): List<Locale> {
        val result = mutableListOf<Locale>()
        result.add(Locale.ROOT) // System default always first

        val codes = readSupportedLocaleCodes(context)

        // Always include English
        if (codes.none { it == "en" }) result.add(Locale.ENGLISH)

        codes
            .map { code ->
                val bcp47 = code.replace("-r", "-")
                Locale.forLanguageTag(bcp47)
            }
            .filter { it.language.isNotEmpty() }
            .sortedBy { it.getDisplayName(it) }
            .forEach { result.add(it) }

        return result
    }

    // -------------------------------------------------------------------------
    // Legacy compatibility stubs — kept so existing call sites don't break.
    // -------------------------------------------------------------------------

    @Deprecated("No longer needed with AppCompatDelegate.setApplicationLocales()")
    val useSystemLanguageSettings: Boolean
        get() = false

    @Deprecated("No longer needed with AppCompatDelegate.setApplicationLocales()")
    @Suppress("UNUSED_PARAMETER")
    fun launchSystemLanguageSettings(context: Context) { /* no-op */ }

    @Deprecated("Use changeLanguage() instead", ReplaceWith("changeLanguage(localeTag)"))
    fun setApplicationLocale(localeTag: String) = changeLanguage(localeTag)

    @Deprecated("No longer needed - AppCompatDelegate.setApplicationLocales() handles this automatically")
    @Suppress("UNUSED_PARAMETER")
    fun restartActivity(context: Context) { /* no-op */ }

    @Deprecated("No longer needed with AppCompatDelegate.setApplicationLocales()")
    fun applyLanguage(context: Context): Context = context
}
