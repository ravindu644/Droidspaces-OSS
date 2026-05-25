package com.droidspaces.app.ui.viewmodel

import android.app.Application
import android.net.Uri
import android.os.Environment
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.droidspaces.app.util.DownloadStatus
import com.droidspaces.app.util.RepoResult
import com.droidspaces.app.util.RootfsAsset
import com.droidspaces.app.util.RootfsDownloadManager
import com.droidspaces.app.util.RootfsRepository
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import java.io.File

sealed class RepoUiState {
    data object Idle    : RepoUiState()
    // carries previous assets so UI can stay expanded while refreshing
    data class  Loading(val previousAssets: List<RootfsAsset> = emptyList()) : RepoUiState()
    data class  Success(val assets: List<RootfsAsset>) : RepoUiState()
    data class  Error(val message: String) : RepoUiState()
}

// Per-asset download state
sealed class AssetDownloadState {
    data object Idle                        : AssetDownloadState()
    data class  Downloading(val percent: Int) : AssetDownloadState()
    data class  Done(val uri: Uri)          : AssetDownloadState()
    data class  Failed(val reason: String)  : AssetDownloadState()
}

class RootfsRepoViewModel(application: Application) : AndroidViewModel(application) {

    var uiState by mutableStateOf<RepoUiState>(RepoUiState.Idle)
        private set

    // per-asset download progress keyed by asset name
    var downloadStates by mutableStateOf<Map<String, AssetDownloadState>>(emptyMap())
        private set

    private val downloadJobs = mutableMapOf<String, Job>()
    private val downloadIds  = mutableMapOf<String, Long>()

    fun load() {
        if (uiState is RepoUiState.Loading) return
        val prev = (uiState as? RepoUiState.Success)?.assets ?: emptyList()
        uiState = RepoUiState.Loading(prev)
        viewModelScope.launch {
            when (val result = RootfsRepository.fetchAssets(getApplication())) {
                is RepoResult.Success -> {
                    val ctx = getApplication<Application>()
                    val prePopulated = result.assets.mapNotNull { asset ->                        val uri = findDownloadedUri(ctx, asset.name)
                        if (uri != null) asset.name to AssetDownloadState.Done(uri) else null
                    }.toMap()
                    // Only preserve active downloads; Done/Failed states are re-derived
                    // from the filesystem via prePopulated so deleted files revert to Idle
                    downloadStates = prePopulated + downloadStates.filter { it.value is AssetDownloadState.Downloading }
                    uiState = RepoUiState.Success(result.assets)
                }
                is RepoResult.Error -> uiState = RepoUiState.Error(result.message)
            }
        }
    }

    /**
     * Looks up a content:// URI for a previously downloaded file by name.
     * Queries DownloadManager first, then checks the file directly.
     * Verifies that the URI points to an existing, readable, and non-empty file.
     * Returns null if not found, deleted, or unreadable.
     */
    private fun findDownloadedUri(ctx: android.content.Context, fileName: String): Uri? {
        val dm = ctx.getSystemService(android.content.Context.DOWNLOAD_SERVICE) as android.app.DownloadManager
        val query = android.app.DownloadManager.Query()
            .setFilterByStatus(android.app.DownloadManager.STATUS_SUCCESSFUL)
        val cursor = dm.query(query) ?: return null
        var resultUri: Uri? = null
        cursor.use {
            val idCol    = it.getColumnIndex(android.app.DownloadManager.COLUMN_ID)
            val titleCol = it.getColumnIndex(android.app.DownloadManager.COLUMN_TITLE)
            while (it.moveToNext()) {
                if (it.getString(titleCol) == fileName) {
                    val id = it.getLong(idCol)
                    val uri = dm.getUriForDownloadedFile(id)
                    if (uri != null && isUriValidAndNotEmpty(ctx, uri)) {
                        resultUri = uri
                        break
                    }
                }
            }
        }
        if (resultUri != null) return resultUri

        // Fallback: file exists in Downloads but not tracked by DownloadManager
        val file = File(
            Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS),
            fileName
        )
        val fallbackUri = if (file.exists() && file.length() > 0) Uri.fromFile(file) else null
        if (fallbackUri != null && isUriValidAndNotEmpty(ctx, fallbackUri)) {
            return fallbackUri
        }
        return null
    }

    private fun isUriValidAndNotEmpty(ctx: android.content.Context, uri: Uri): Boolean {
        return try {
            ctx.contentResolver.openAssetFileDescriptor(uri, "r")?.use { fd ->
                fd.length > 0 || fd.length == android.content.res.AssetFileDescriptor.UNKNOWN_LENGTH
            } ?: false
        } catch (e: Exception) {
            false
        }
    }

    fun startDownload(asset: RootfsAsset) {
        if (downloadStates[asset.name] is AssetDownloadState.Downloading) return
        val ctx = getApplication<Application>()
        downloadJobs[asset.name]?.cancel()
        val downloadId = RootfsDownloadManager.enqueue(ctx, asset)
        downloadIds[asset.name] = downloadId
        downloadJobs[asset.name] = viewModelScope.launch {
            RootfsDownloadManager.pollFlow(ctx, asset, downloadId).collect { status ->
                downloadStates = downloadStates.toMutableMap().apply {
                    put(asset.name, when (status) {
                        is DownloadStatus.Progress  -> AssetDownloadState.Downloading(status.percent)
                        is DownloadStatus.Completed -> AssetDownloadState.Done(status.fileUri)
                        is DownloadStatus.Failed    -> AssetDownloadState.Failed(status.reason)
                    })
                }
            }
        }
    }

    fun cancelDownload(asset: RootfsAsset) {
        val ctx = getApplication<Application>()
        downloadIds[asset.name]?.let { RootfsDownloadManager.cancel(ctx, it) }
        downloadIds.remove(asset.name)
        downloadJobs[asset.name]?.cancel()
        downloadJobs.remove(asset.name)
        downloadStates = downloadStates.toMutableMap().apply { put(asset.name, AssetDownloadState.Idle) }
    }

    /** Reset a completed/failed asset so the user can retry. */
    fun resetAsset(assetName: String) {
        downloadStates = downloadStates.toMutableMap().apply { put(assetName, AssetDownloadState.Idle) }
    }
}
