package com.droidspaces.app.util

import android.content.Context
import android.util.Log
import com.topjohnwu.superuser.Shell
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.File

/**
 * What the running kernel can do, from one `droidspaces --format check` call.
 * The config form greys out what the host cannot honour, and [coerce] fixes a
 * saved config that already holds such a value.
 */
data class HostCapabilities(
    val flags: Map<String, Boolean>,
    val requirementsMet: Boolean,
    val backendVersion: String
) {
    /** A key the backend did not report means an older build, so the control stays enabled. */
    fun has(key: String): Boolean = flags[key] ?: true

    private val natSupported get() = has("net_ns") && has("veth")

    /** NAT needs a network namespace and veth, gateway additionally a bridge. */
    fun supportedNetModes(): List<String> = ALL_NET_MODES.filter {
        when (it) {
            "nat" -> natSupported
            "gateway" -> natSupported && has("bridge")
            else -> true
        }
    }

    /**
     * Every correction collected into one copy: unsupported features off, IPv6
     * forced off in NAT without IPv6 NAT, an unsupported network mode back to host.
     * Limits and the requested user-namespace setting are retained. The form shows
     * the latter checked but disabled when the kernel cannot provide it.
     */
    fun coerce(s: ContainerConfigState): ContainerConfigState {
        val mode = if (s.netMode in supportedNetModes()) s.netMode else "host"
        return s.copy(
            netMode = mode,
            enableHwAccess = s.enableHwAccess && has("devtmpfs"),
            volatileMode = s.volatileMode && has("overlayfs"),
            forceCgroupv1 = s.forceCgroupv1 && has("cgroup2"),
            disableIPv6 = s.disableIPv6 || (mode == "nat" && !has("ipv6_nat"))
        )
    }

    companion object {
        private const val TAG = "HostCapabilities"
        val ALL_NET_MODES = listOf("nat", "host", "none", "gateway")

        private val _state = MutableStateFlow<HostCapabilities?>(null)

        /** Null until the first probe or cache load; nothing is greyed out before then. */
        val state: StateFlow<HostCapabilities?> = _state.asStateFlow()

        private const val BOOT_ID_PATH = "/proc/sys/kernel/random/boot_id"

        /** Read as the app, no root. Vendor SELinux policy may deny it, so empty means unknown. */
        private fun appBootId(): String =
            try { File(BOOT_ID_PATH).readText().trim() } catch (e: Exception) { "" }

        /** Read as root. Only called once root and the backend are known good. */
        private fun rootBootId(): String =
            try { Shell.cmd("cat $BOOT_ID_PATH").exec().out.firstOrNull()?.trim().orEmpty() } catch (e: Exception) { "" }

        /** The app read is free; the root shell is the fallback for a policy that denies it. */
        private fun bootId(): String = appBootId().ifEmpty { rootBootId() }

        private fun parse(json: String): HostCapabilities? = try {
            val obj = JSONObject(json)
            val flags = buildMap {
                for (key in obj.keys()) (obj.opt(key) as? Number)?.let { put(key, it.toInt() != 0) }
            }
            HostCapabilities(flags, obj.optInt("requirements_met", 1) != 0, obj.optString("version"))
        } catch (e: Exception) {
            null
        }

        /**
         * Fast seed at process start, before root exists: trust the saved copy only
         * if the app can read the boot id itself and it matches. An unreadable id
         * must never match, so a denied read just leaves the seeding to
         * [refreshIfStale].
         */
        fun load(context: Context) {
            val prefs = PreferencesManager.getInstance(context)
            val boot = appBootId()
            if (boot.isEmpty() || prefs.cachedHostCapabilitiesBootId != boot) return
            prefs.cachedHostCapabilities?.let(::parse)?.let { _state.value = it }
        }

        /**
         * Runs once root and the backend are available, so the boot id can fall
         * back to a root read and SELinux cannot hide a reboot from us. Same boot
         * and same backend version: reuse the saved copy. Anything else: probe.
         */
        suspend fun refreshIfStale(context: Context) = withContext(Dispatchers.IO) {
            val prefs = PreferencesManager.getInstance(context)
            val boot = bootId()
            val sameBoot = boot.isNotEmpty() && boot == prefs.cachedHostCapabilitiesBootId
            val cached = if (sameBoot) prefs.cachedHostCapabilities?.let(::parse) else null
            val installed = SystemInfoManager.getCachedDroidspacesVersion(context)?.removePrefix("v")
            if (cached != null && (installed == null || installed == cached.backendVersion)) {
                if (_state.value == null) _state.value = cached
                return@withContext
            }
            refresh(context)
        }

        /** One root call. An older backend prints the text report here, which fails to parse and leaves the state alone. */
        suspend fun refresh(context: Context) = withContext(Dispatchers.IO) {
            try {
                val result = Shell.cmd(ContainerCommandBuilder.buildCheckCommand()).exec()
                if (!result.isSuccess) return@withContext
                val json = result.out.joinToString("")
                val caps = parse(json) ?: return@withContext
                _state.value = caps
                PreferencesManager.getInstance(context).apply {
                    cachedHostCapabilities = json
                    cachedHostCapabilitiesBootId = bootId()
                }
            } catch (e: Exception) {
                Log.w(TAG, "check --format failed", e)
            }
        }
    }
}
