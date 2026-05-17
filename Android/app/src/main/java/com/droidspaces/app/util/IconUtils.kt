package com.droidspaces.app.util

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Storage
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.painter.Painter
import androidx.compose.ui.graphics.vector.rememberVectorPainter
import androidx.compose.ui.res.painterResource
import com.droidspaces.app.R

object IconUtils {

    // Returns drawable res ID for a distro name, or null for fallback (Storage icon).
    // Safe to call from any thread — no Compose context needed.
    fun getDistroIconRes(name: String?): Int? {
        val s = name ?: return null
        return when {
            s.contains("Ubuntu", true) -> R.drawable.ic_ubuntu
            s.contains("Debian", true) -> R.drawable.ic_debian
            s.contains("Alpine", true) -> R.drawable.ic_alpine
            s.contains("Arch", true) -> R.drawable.ic_arch
            s.contains("Fedora", true) -> R.drawable.ic_fedora
            s.contains("NixOS", true) -> R.drawable.ic_nixos
            s.contains("OpenWrt", true) -> R.drawable.ic_openwrt
            s.contains("Gentoo", true) -> R.drawable.ic_gentoo
            s.contains("Devuan", true) -> R.drawable.ic_devuan
            s.contains("Kali", true) -> R.drawable.ic_kali
            s.contains("Suse", true) -> R.drawable.ic_suse
            s.contains("CentOS", true) -> R.drawable.ic_centos
            s.contains("Rocky", true) -> R.drawable.ic_rocky
            s.contains("Alma", true) -> R.drawable.ic_almalinux
            s.contains("Red", true) || s.contains("RHEL", true) -> R.drawable.ic_redhat
            s.contains("Void", true) -> R.drawable.ic_void
            s.contains("Manjaro", true) -> R.drawable.ic_manjaro
            s.contains("Raspberry", true) || s.contains("Raspbian", true) -> R.drawable.ic_raspberry
            s.contains("BusyBox", true) -> R.drawable.ic_busybox
            s.contains("FreeBSD", true) -> R.drawable.ic_freebsd
            s.contains("Slackware", true) -> R.drawable.ic_slackware
            s.contains("Mint", true) -> R.drawable.ic_mint
            else -> null
        }
    }

    @Composable
    fun getDistroIcon(name: String?): Painter {
        val res = getDistroIconRes(name)
        return if (res != null) painterResource(id = res)
        else rememberVectorPainter(image = Icons.Default.Storage)
    }
}
