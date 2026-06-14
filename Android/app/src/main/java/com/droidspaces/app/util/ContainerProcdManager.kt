package com.droidspaces.app.util

import android.util.Log
import com.topjohnwu.superuser.Shell
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * OpenWrt/procd service manager for containers.
 *
 * This initial manager only detects OpenWrt/procd availability. Service listing
 * and actions are added separately so detection/navigation can be tested first.
 */
object ContainerProcdManager {
    private const val TAG = "ContainerProcdManager"

    /**
     * Check if the container looks like an OpenWrt/procd system.
     *
     * Do not use /etc/init.d alone: OpenRC and SysV-style systems also expose
     * that directory. /etc/openwrt_release is the distro-level discriminator;
     * procd/ubus/service binaries confirm that the OpenWrt service stack exists.
     */
    suspend fun isProcdAvailable(containerName: String): Boolean = withContext(Dispatchers.IO) {
        try {
            val cmd = "${Constants.DROIDSPACES_BINARY_PATH} --name=${ContainerCommandBuilder.quote(containerName)} run '[ -f /etc/openwrt_release ] && { [ -x /sbin/procd ] || [ -x /sbin/ubusd ] || command -v ubus >/dev/null 2>&1 || command -v service >/dev/null 2>&1; }'"
            Shell.cmd(cmd).exec().isSuccess
        } catch (e: Exception) {
            Log.e(TAG, "Error checking procd availability", e)
            false
        }
    }
}
