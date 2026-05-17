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
import com.droidspaces.app.util.PortForward

class ContainerInstallationViewModel : ViewModel() {
    var tarballUri: Uri? by mutableStateOf(null)
        private set

    var containerName: String by mutableStateOf("")
        private set

    var hostname: String by mutableStateOf("")
        private set

    var netMode: String by mutableStateOf("host")
        private set

    var disableIPv6: Boolean by mutableStateOf(false)
        private set

    var enableAndroidStorage: Boolean by mutableStateOf(false)
        private set

    var enableHwAccess: Boolean by mutableStateOf(false)
        private set

    var enableGpuMode: Boolean by mutableStateOf(false)
        private set

    var enableTermuxX11: Boolean by mutableStateOf(false)
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

    var customInit: String by mutableStateOf("")
        private set

    var staticNatIp: String by mutableStateOf("")
        private set

    var envFileContent: String? by mutableStateOf(null)
        private set

    var useSparseImage: Boolean by mutableStateOf(true)
        private set

    var sparseImageSizeGB: Int by mutableStateOf(8)
        private set

    var upstreamInterfaces: List<String> by mutableStateOf(emptyList())
        private set

    var portForwards: List<PortForward> by mutableStateOf(emptyList())
        private set

    var forceCgroupv1: Boolean by mutableStateOf(false)
        private set

    var blockNestedNs: Boolean by mutableStateOf(false)
        private set

    var privileged: String by mutableStateOf("")
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
        netMode: String,
        disableIPv6: Boolean,
        enableAndroidStorage: Boolean,
        enableHwAccess: Boolean,
        enableGpuMode: Boolean,
        enableTermuxX11: Boolean,
        selinuxPermissive: Boolean,
        volatileMode: Boolean,
        bindMounts: List<BindMount>,
        dnsServers: String,
        runAtBoot: Boolean,
        customInit: String,
        staticNatIp: String,
        envFileContent: String?,
        upstreamInterfaces: List<String>,
        portForwards: List<PortForward>,
        forceCgroupv1: Boolean,
        blockNestedNs: Boolean,
        privileged: String
    ) {
        this.netMode = netMode
        this.disableIPv6 = disableIPv6
        this.enableAndroidStorage = enableAndroidStorage
        this.enableHwAccess = enableHwAccess
        this.enableGpuMode = enableGpuMode
        this.enableTermuxX11 = enableTermuxX11
        this.selinuxPermissive = selinuxPermissive
        this.volatileMode = volatileMode
        this.bindMounts = bindMounts
        this.dnsServers = dnsServers
        this.runAtBoot = runAtBoot
        this.customInit = customInit
        this.staticNatIp = staticNatIp
        this.envFileContent = envFileContent
        this.upstreamInterfaces = upstreamInterfaces
        this.portForwards = portForwards
        this.forceCgroupv1 = forceCgroupv1
        this.blockNestedNs = blockNestedNs
        this.privileged = privileged
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
            netMode = netMode,
            disableIPv6 = disableIPv6,
            enableAndroidStorage = enableAndroidStorage,
            enableHwAccess = enableHwAccess,
            enableGpuMode = enableGpuMode,
            enableTermuxX11 = enableTermuxX11,
            selinuxPermissive = selinuxPermissive,
            volatileMode = volatileMode,
            bindMounts = bindMounts,
            dnsServers = dnsServers,
            runAtBoot = runAtBoot,
            customInit = customInit,
            staticNatIp = staticNatIp,
            envFileContent = envFileContent,
            status = ContainerStatus.STOPPED, // Default status for new container
            useSparseImage = useSparseImage,
            sparseImageSizeGB = if (useSparseImage) sparseImageSizeGB else null,
            upstreamInterfaces = upstreamInterfaces,
            portForwards = portForwards,
            forceCgroupv1 = forceCgroupv1,
            blockNestedNs = blockNestedNs,
            privileged = privileged
        )
    }

    fun reset() {
        tarballUri = null
        containerName = ""
        hostname = ""
        netMode = "host"
        disableIPv6 = false
        enableAndroidStorage = false
        enableHwAccess = false
        enableGpuMode = false
        enableTermuxX11 = false
        selinuxPermissive = false
        volatileMode = false
        bindMounts = emptyList()
        dnsServers = ""
        runAtBoot = false
        customInit = ""
        staticNatIp = ""
        envFileContent = null
        useSparseImage = true
        sparseImageSizeGB = 8
        upstreamInterfaces = emptyList()
        portForwards = emptyList()
        forceCgroupv1 = false
        blockNestedNs = false
        privileged = ""
    }
}

