package com.droidspaces.app.ui.viewmodel

import android.net.Uri
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel
import com.droidspaces.app.util.ContainerInfo
import com.droidspaces.app.util.ContainerManager
import com.droidspaces.app.util.ContainerStatus
import com.droidspaces.app.util.Constants

import com.droidspaces.app.util.BindMount

class ContainerInstallationViewModel : ViewModel() {
    var tarballUri: Uri? by mutableStateOf(null)
        private set

    var containerName: String by mutableStateOf("")
        private set

    var hostname: String by mutableStateOf("")
        private set

    var enableIPv6: Boolean by mutableStateOf(false)
        private set

    var enableAndroidStorage: Boolean by mutableStateOf(false)
        private set

    var enableHwAccess: Boolean by mutableStateOf(false)
        private set

    var selinuxPermissive: Boolean by mutableStateOf(false)
        private set

    var volatileMode: Boolean by mutableStateOf(false)
        private set

    var bindMounts: List<BindMount> by mutableStateOf(emptyList())
        private set
    
    var dnsServers: String by mutableStateOf("")
        private set

    var runAtBoot: Boolean by mutableStateOf(false)
        private set

    var disableSeccompFilter: Boolean by mutableStateOf(false)
        private set

    var useSparseImage: Boolean by mutableStateOf(false)
        private set

    var sparseImageSizeGB: Int by mutableStateOf(8)
        private set

    fun setTarball(uri: Uri) {
        tarballUri = uri
    }

    fun setName(name: String, hostname: String) {
        this.containerName = name
        this.hostname = hostname
    }

    fun setSparseImageConfig(useSparseImage: Boolean, sizeGB: Int) {
        this.useSparseImage = useSparseImage
        this.sparseImageSizeGB = sizeGB
    }

    fun setConfig(
        enableIPv6: Boolean,
        enableAndroidStorage: Boolean,
        enableHwAccess: Boolean,
        selinuxPermissive: Boolean,
        volatileMode: Boolean,
        bindMounts: List<BindMount>,
        dnsServers: String,
        runAtBoot: Boolean,
        disableSeccompFilter: Boolean
    ) {
        this.enableIPv6 = enableIPv6
        this.enableAndroidStorage = enableAndroidStorage
        this.enableHwAccess = enableHwAccess
        this.selinuxPermissive = selinuxPermissive
        this.volatileMode = volatileMode
        this.bindMounts = bindMounts
        this.dnsServers = dnsServers
        this.runAtBoot = runAtBoot
        this.disableSeccompFilter = disableSeccompFilter
    }

    fun buildConfig(): ContainerInfo? {
        if (tarballUri == null) return null
        if (containerName.isEmpty()) return null

        return ContainerInfo(
            name = containerName,
            hostname = hostname.ifEmpty { containerName },
            rootfsPath = if (useSparseImage) {
                ContainerManager.getSparseImagePath(containerName)
            } else {
                ContainerManager.getRootfsPath(containerName)
            },
            enableIPv6 = enableIPv6,
            enableAndroidStorage = enableAndroidStorage,
            enableHwAccess = enableHwAccess,
            selinuxPermissive = selinuxPermissive,
            volatileMode = volatileMode,
            bindMounts = bindMounts,
            dnsServers = dnsServers,
            runAtBoot = runAtBoot,
            status = ContainerStatus.STOPPED, // Default status for new container
            useSparseImage = useSparseImage,
            sparseImageSizeGB = if (useSparseImage) sparseImageSizeGB else null,
            disableSeccompFilter = disableSeccompFilter
        )
    }

    fun reset() {
        tarballUri = null
        containerName = ""
        hostname = ""
        enableIPv6 = false
        enableAndroidStorage = false
        enableHwAccess = false
        selinuxPermissive = false
        volatileMode = false
        bindMounts = emptyList()
        dnsServers = ""
        runAtBoot = false
        disableSeccompFilter = false
        useSparseImage = false
        sparseImageSizeGB = 8
    }
}

