package com.droidspaces.app.ui.viewmodel

import android.app.Application
import android.net.Uri
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.droidspaces.app.service.AssetDownloadState
import com.droidspaces.app.service.RootfsDownloadService
import com.droidspaces.app.util.PreferencesManager
import com.droidspaces.app.util.RepoResult
import com.droidspaces.app.util.RootfsAsset
import com.droidspaces.app.util.RootfsRepository
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

sealed class RepoUiState {
    data object Idle    : RepoUiState()
    // carries previous assets so UI can stay expanded while refreshing
    data class  Loading(val previousAssets: List<RootfsAsset> = emptyList()) : RepoUiState()
    data class  Success(val assets: List<RootfsAsset>) : RepoUiState()
    data class  Error(val message: String) : RepoUiState()
}

class RootfsRepoViewModel(application: Application) : AndroidViewModel(application) {

    var uiState by mutableStateOf<RepoUiState>(RepoUiState.Idle)
        private set

    // per-asset download progress keyed by download URL
    var downloadStates by mutableStateOf<Map<String, AssetDownloadState>>(emptyMap())
        private set

    // Assets already sitting in Downloads, live service state overrides these
    private var found: Map<String, AssetDownloadState> = emptyMap()

    init {
        viewModelScope.launch {
            RootfsDownloadService.states.collect { downloadStates = found + it }
        }
    }

    fun load() {
        if (uiState is RepoUiState.Loading) return
        val prev = (uiState as? RepoUiState.Success)?.assets ?: emptyList()
        uiState = RepoUiState.Loading(prev)
        viewModelScope.launch {
            when (val result = RootfsRepository.fetchAllAssets(getApplication())) {
                is RepoResult.Success -> {
                    // Emit Success immediately so the UI renders and the
                    // loading spinner stops and cards appear without delay.
                    uiState = RepoUiState.Success(result.assets)

                    // Heavy filesystem scan runs off the main thread so the
                    // Compose frame loop stays unblocked during the lookup.
                    val ctx = getApplication<Application>()
                    found = withContext(Dispatchers.IO) {
                        result.assets.mapNotNull { asset ->
                            val uri = RootfsDownloadService.findDownloaded(ctx, asset.uniqueFilename)
                                ?.takeIf { isUriValidAndNotEmpty(ctx, it) }
                            if (uri != null) asset.downloadUrl to AssetDownloadState.Done(uri) else null
                        }.toMap()
                    }
                    // A finished download is re-derived from disk like everything else, so a
                    // tarball deleted since then reverts to Download instead of a dead Install
                    RootfsDownloadService.states.value
                        .filterValues { it is AssetDownloadState.Done }
                        .keys.forEach { RootfsDownloadService.set(it, null) }
                    downloadStates = found + RootfsDownloadService.states.value
                }
                is RepoResult.Error -> uiState = RepoUiState.Error(result.message)
            }
        }
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
        if (downloadStates[asset.downloadUrl] is AssetDownloadState.Downloading) return
        RootfsDownloadService.start(getApplication(), asset)
    }

    fun cancelDownload(asset: RootfsAsset) = RootfsDownloadService.cancel(getApplication(), asset.downloadUrl)

    fun failDownload(asset: RootfsAsset, reason: String) =
        RootfsDownloadService.set(asset.downloadUrl, AssetDownloadState.Failed(reason))

    /** Reset a completed/failed asset so the user can retry. */
    fun resetAsset(url: String) = RootfsDownloadService.set(url, null)

    fun addCustomRepo(name: String, url: String) {
        PreferencesManager.getInstance(getApplication()).addCustomRepo(name, url)
        load()
    }

    fun removeCustomRepo(url: String) {
        PreferencesManager.getInstance(getApplication()).removeCustomRepo(url)
        load()
    }

    fun getCustomRepos(): List<Pair<String, String>> =
        PreferencesManager.getInstance(getApplication()).getCustomRepos()
}
