package com.droidspaces.app.util

import android.content.Context
import com.droidspaces.app.R
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONArray
import java.net.HttpURLConnection
import java.net.URL

data class RootfsAsset(
    val name: String,
    val downloadUrl: String,
    val sizeBytes: Long,
    val downloadCount: Int
)

sealed class RepoResult {
    data class Success(val assets: List<RootfsAsset>) : RepoResult()
    data class Error(val message: String) : RepoResult()
}

object RootfsRepository {

    private const val API_URL =
        "https://api.github.com/repos/Droidspaces/Droidspaces-rootfs-builder/releases/latest"
    private const val CONNECT_TIMEOUT = 10_000
    private const val READ_TIMEOUT    = 15_000

    suspend fun fetchAssets(context: Context): RepoResult = withContext(Dispatchers.IO) {
        runCatching {
            val conn = (URL(API_URL).openConnection() as HttpURLConnection).apply {
                connectTimeout = CONNECT_TIMEOUT
                readTimeout    = READ_TIMEOUT
                setRequestProperty("Accept", "application/vnd.github+json")
                setRequestProperty("X-GitHub-Api-Version", "2022-11-28")
            }

            val code = conn.responseCode
            if (code != 200) {
                return@runCatching RepoResult.Error(
                    context.getString(R.string.repo_error_server_http, code)
                )
            }

            val json = conn.inputStream.bufferedReader().readText()
            conn.disconnect()

            val assets = parseAssets(json)
            if (assets.isEmpty()) {
                RepoResult.Error(context.getString(R.string.repo_error_no_assets))
            } else {
                RepoResult.Success(assets)
            }
        }.getOrElse { e ->
            RepoResult.Error(e.message ?: context.getString(R.string.repo_error_network))
        }
    }

    private fun parseAssets(json: String): List<RootfsAsset> {
        val arr: JSONArray = org.json.JSONObject(json).optJSONArray("assets")
            ?: return emptyList()

        return buildList {
            for (i in 0 until arr.length()) {
                val obj = arr.getJSONObject(i)
                val name = obj.optString("name", "")
                // only include actual tarballs
                if (!name.endsWith(".tar.gz") && !name.endsWith(".tar.xz")) continue
                add(
                    RootfsAsset(
                        name          = name,
                        downloadUrl   = obj.optString("browser_download_url", ""),
                        sizeBytes     = obj.optLong("size", 0L),
                        downloadCount = obj.optInt("download_count", 0)
                    )
                )
            }
        }
    }
}
