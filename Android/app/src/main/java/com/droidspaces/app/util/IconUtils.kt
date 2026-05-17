package com.droidspaces.app.util

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Storage
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.painter.Painter
import androidx.compose.ui.graphics.vector.rememberVectorPainter
import androidx.compose.ui.res.painterResource
import com.droidspaces.app.R

object IconUtils {
    @Composable
    fun getDistroIcon(name: String?): Painter {
        val searchStr = name ?: ""
        return when {
            searchStr.contains("Ubuntu", true) -> painterResource(id = R.drawable.ic_ubuntu)
            searchStr.contains("Debian", true) -> painterResource(id = R.drawable.ic_debian)
            searchStr.contains("Alpine", true) -> painterResource(id = R.drawable.ic_alpine)
            searchStr.contains("Arch", true) -> painterResource(id = R.drawable.ic_arch)
            searchStr.contains("Fedora", true) -> painterResource(id = R.drawable.ic_fedora)
            searchStr.contains("NixOS", true) -> painterResource(id = R.drawable.ic_nixos)
            searchStr.contains("OpenWrt", true) -> painterResource(id = R.drawable.ic_openwrt)
            searchStr.contains("Gentoo", true) -> painterResource(id = R.drawable.ic_gentoo)
            searchStr.contains("Devuan", true) -> painterResource(id = R.drawable.ic_devuan)
            searchStr.contains("Kali", true) -> painterResource(id = R.drawable.ic_kali)
            searchStr.contains("Suse", true) -> painterResource(id = R.drawable.ic_suse)
            searchStr.contains("CentOS", true) -> painterResource(id = R.drawable.ic_centos)
            searchStr.contains("Rocky", true) -> painterResource(id = R.drawable.ic_rocky)
            searchStr.contains("Alma", true) -> painterResource(id = R.drawable.ic_almalinux)
            searchStr.contains("Red", true) || searchStr.contains("RHEL", true) -> painterResource(id = R.drawable.ic_redhat)
            searchStr.contains("Void", true) -> painterResource(id = R.drawable.ic_void)
            searchStr.contains("Manjaro", true) -> painterResource(id = R.drawable.ic_manjaro)
            searchStr.contains("Raspberry", true) || searchStr.contains("Raspbian", true) -> painterResource(id = R.drawable.ic_raspberry)
            searchStr.contains("BusyBox", true) -> painterResource(id = R.drawable.ic_busybox)
            searchStr.contains("FreeBSD", true) -> painterResource(id = R.drawable.ic_freebsd)
            searchStr.contains("Slackware", true) -> painterResource(id = R.drawable.ic_slackware)
            searchStr.contains("Mint", true) -> painterResource(id = R.drawable.ic_mint)
            else -> rememberVectorPainter(image = Icons.Default.Storage)
        }
    }
}
