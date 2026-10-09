package com.droidspaces.app.ui.viewmodel

import android.content.Context
import android.net.Uri
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.droidspaces.app.R
import com.droidspaces.app.util.ContainerInfo
import com.droidspaces.app.util.ContainerInstaller
import com.droidspaces.app.util.ContainerManager
import com.droidspaces.app.util.ContainerStatus
import com.droidspaces.app.util.Constants

import com.droidspaces.app.util.ContainerConfigState
import com.droidspaces.app.util.ValidationUtils
import com.droidspaces.app.util.withConfig
import com.droidspaces.app.util.HostCapabilities
import com.droidspaces.app.util.RootfsConfig
import com.droidspaces.app.util.toConfigState
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

class ContainerInstallationViewModel : ViewModel() {
    var tarballUri: Uri? by mutableStateOf(null)
        private set

    var preparedTarball: File? = null
        private set
    var preparingTarball by mutableStateOf(true)
        private set
    var preparationError: String? by mutableStateOf(null)
        private set
    var recommendationNotice: String? by mutableStateOf(null)
        private set
    var recommendedHwAccess by mutableStateOf(false)
        private set
    var recommendedPrivileged by mutableStateOf("")
        private set

    var containerName: String by mutableStateOf("")
        private set

    var hostname: String by mutableStateOf("")
        private set

    var useSparseImage: Boolean by mutableStateOf(true)
        private set

    var sparseImageSizeGB: Int by mutableStateOf(8)
        private set

    /**
     * Destination volume for the rootfs, or null for the default location under
     * CONTAINERS_BASE_PATH. Only the bulk data moves; config and .env stay internal.
     */
    var storageDir: String? by mutableStateOf(null)
        private set

    /** All editable networking/security/advanced config, hoisted as one value. */
    var configState: ContainerConfigState by mutableStateOf(ContainerConfigState())
        private set

    fun setTarball(context: Context, uri: Uri) {
        // Returning to the first wizard page must not reapply defaults over user edits.
        if (tarballUri == uri) return
        tarballUri = uri
        val appContext = context.applicationContext
        viewModelScope.launch {
            try {
                val extension = ContainerInstaller.getTarballExtension(appContext, uri)
                val archive = File.createTempFile("rootfs_", ".tar$extension", appContext.cacheDir)
                preparedTarball = archive
                ContainerInstaller.copyTarball(appContext, uri, archive)
                try {
                    val recommended = withContext(Dispatchers.IO) { RootfsConfig.read(archive) }
                    if (recommended != null) {
                        containerName = ValidationUtils.normalizeContainerName(recommended.name)
                        hostname = recommended.hostname
                        useSparseImage = recommended.useSparseImage
                        sparseImageSizeGB = recommended.sparseImageSizeGB ?: 8
                        val state = recommended.toConfigState().let { HostCapabilities.state.value?.coerce(it) ?: it }
                        recommendedHwAccess = state.enableHwAccess
                        recommendedPrivileged = state.privileged
                        // These two settings keep the same confirmation gates as manual setup.
                        configState = state.copy(enableHwAccess = false, privileged = "")
                        recommendationNotice = appContext.getString(R.string.rootfs_config_loaded)
                    }
                } catch (e: CancellationException) {
                    throw e
                } catch (e: Exception) {
                    recommendationNotice = appContext.getString(R.string.rootfs_config_invalid, e.message.orEmpty())
                }
            } catch (e: CancellationException) {
                preparedTarball?.delete()
                throw e
            } catch (e: Exception) {
                preparationError = e.message ?: appContext.getString(R.string.operation_failed_title)
                preparedTarball?.delete()
            } finally {
                preparingTarball = false
            }
        }
    }

    fun confirmRecommendedHwAccess(enabled: Boolean) {
        configState = configState.copy(enableHwAccess = enabled)
        recommendedHwAccess = false
    }

    fun confirmRecommendedPrivileged(tags: String) {
        configState = configState.copy(privileged = tags)
        recommendedPrivileged = ""
    }

    fun setName(name: String, hostname: String) {
        this.containerName = name
        this.hostname = hostname
    }

    fun setSparseImageConfig(useSparseImage: Boolean, sizeGB: Int, storageDir: String? = null) {
        this.useSparseImage = useSparseImage
        this.sparseImageSizeGB = sizeGB
        this.storageDir = storageDir
    }

    fun setConfig(config: ContainerConfigState) {
        this.configState = config
    }

    fun buildConfig(): ContainerInfo? {
        if (tarballUri == null) return null
        if (containerName.isEmpty()) return null

        return ContainerInfo(
            name = containerName,
            hostname = hostname.ifEmpty { ValidationUtils.sanitizeHostname(containerName) },
            rootfsPath = if (useSparseImage) {
                ContainerManager.getSparseImagePath(containerName, storageDir)
            } else {
                ContainerManager.getRootfsPath(containerName, storageDir)
            },
            status = ContainerStatus.STOPPED, // Default status for new container
            useSparseImage = useSparseImage,
            sparseImageSizeGB = if (useSparseImage) sparseImageSizeGB else null,
        ).withConfig(HostCapabilities.state.value?.coerce(configState) ?: configState)
    }

    fun reset() {
        preparedTarball?.delete()
        preparedTarball = null
        tarballUri = null
        preparingTarball = true
        preparationError = null
        recommendationNotice = null
        recommendedHwAccess = false
        recommendedPrivileged = ""
        containerName = ""
        hostname = ""
        useSparseImage = true
        sparseImageSizeGB = 8
        storageDir = null
        configState = ContainerConfigState()
    }

    override fun onCleared() {
        preparedTarball?.delete()
        super.onCleared()
    }
}
