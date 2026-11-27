package com.droidspaces.app.util

import android.content.Context
import android.content.res.Configuration
import androidx.appcompat.app.AppCompatDelegate
import androidx.core.os.LocaleListCompat
import com.droidspaces.app.R
import org.xmlpull.v1.XmlPullParser
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
 * The list of supported languages comes from the locale config AGP generates
 * from the res/values-* folders (generateLocaleConfig in build.gradle.kts), so
 * a translation merged from Weblate is in the picker on the next build.
 */
object LocaleHelper {

    /** The locales in the generated res/xml/_generated_res_locale_config.xml. */
    private fun readSupportedLocales(context: Context): List<Locale> {
        val parser = context.resources.getXml(R.xml._generated_res_locale_config)
        val locales = mutableListOf<Locale>()
        try {
            while (parser.next() != XmlPullParser.END_DOCUMENT) {
                if (parser.eventType != XmlPullParser.START_TAG || parser.name != "locale") continue
                parser.getAttributeValue("http://schemas.android.com/apk/res/android", "name")
                    ?.let { locales += Locale.forLanguageTag(it) }
            }
        } finally {
            parser.close()
        }
        return locales
    }

    /**
     * The `lang` or `lang-rCC` code this object keys languages by. It comes from
     * Locale.language, so the legacy code a folder may use (Indonesian is
     * values-in) and the current one a tag may use (id) land on the same string,
     * which is also what the current app locale reports.
     */
    private fun codeOf(locale: Locale): String =
        if (locale.country.isNotEmpty()) "${locale.language}-r${locale.country}" else locale.language

    /** Display names come from the Locale API, so a new language needs no name strings. */
    private fun localeToLanguage(locale: Locale): Language {
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

        val code = codeOf(locale)
        return Language(
            code = code,
            displayName = englishName.ifBlank { code },
            nativeName = nativeName.ifBlank { code }
        )
    }

    /**
     * Get available languages for the in-app language picker.
     *
     * The list is the generated locale config, sorted by English display name.
     *
     * English is always included as the first entry (it is the app's default /
     * fallback language and does not need a values-en directory).
     */
    fun getAvailableLanguages(context: Context): List<Language> {
        // English is the default (resources.properties); it leads the list unsorted
        return listOf(Language("en", "English", "English")) + readSupportedLocales(context)
            .filter { it.language != "en" }
            .map { localeToLanguage(it) }
            .sortedBy { it.displayName }
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

        return codeOf(locale)
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

}
