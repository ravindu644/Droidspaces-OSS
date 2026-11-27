package com.droidspaces.app.util

import android.content.Context
import org.json.JSONObject

data class AppUpdateInfo(
    val version: String,
    val releaseUrl: String
)

object AppUpdateChecker {
    private const val LATEST_RELEASE_URL =
        "https://api.github.com/repos/ravindu644/Droidspaces-OSS/releases/latest"

    // Blocking, call from Dispatchers.IO. Null when the installed build is
    // already current and on any failure, so the banner just stays hidden: an
    // offline device should see nothing, not a warning.
    fun fetchLatest(context: Context): AppUpdateInfo? = runCatching {
        val body = RootfsRepository.httpGet(LATEST_RELEASE_URL) ?: return null
        val json = JSONObject(body)
        val tag = json.optString("tag_name")
        val installed = context.packageManager.getPackageInfo(context.packageName, 0).versionName
        val latest = versionNumber(tag) ?: return null
        val current = versionNumber(installed ?: return null) ?: return null
        if (latest <= current) return null
        AppUpdateInfo(version = tag, releaseUrl = json.optString("html_url"))
    }.getOrNull()

    // Release tags are vX.Y.Z and the installed versionName is X.Y.Z, straight
    // from DS_VERSION in droidspaces.h. Pull the first dotted triple out of
    // either, so a prefix or suffix on the tag never breaks the compare.
    private fun versionNumber(text: String): Int? =
        Regex("(\\d+)\\.(\\d+)\\.(\\d+)").find(text)?.let { m ->
            val (major, minor, patch) = m.destructured
            major.toInt() * 10_000 + minor.toInt() * 100 + patch.toInt()
        }
}
