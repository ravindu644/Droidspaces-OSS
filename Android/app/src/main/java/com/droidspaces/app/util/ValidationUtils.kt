package com.droidspaces.app.util

import android.content.Context
import com.droidspaces.app.R

/**
 * Centralized validation utilities to eliminate duplication.
 * All validation logic in one place for consistency and maintainability.
 */
object ValidationUtils {
    /**
     * Normalizes a container name before it is stored or used to build paths.
     * Trims leading/trailing whitespace and collapses internal whitespace runs to a
     * single space, so that e.g. "alpine " -> "alpine" and "alpine    container" ->
     * "alpine container". This must run BEFORE sanitizeContainerName so the directory
     * name, the config "name=" field, and the --name passed to the backend all agree.
     */
    fun normalizeContainerName(name: String): String {
        return name.trim().replace(Regex("\\s+"), " ")
    }

    /**
     * Validates container name: letters, numbers, hyphens, underscores, spaces, and dots allowed.
     */
    fun validateContainerName(name: String, context: Context? = null): ValidationResult {
        return when {
            name.isEmpty() -> {
                val message = context?.getString(R.string.error_container_name_empty)
                    ?: "Container name cannot be empty"
                ValidationResult.Error(message)
            }
            !name.matches(Regex("^[a-zA-Z0-9_\\s.-]+$")) -> {
                val message = context?.getString(R.string.error_container_name_invalid)
                    ?: "Container name can only contain letters, numbers, hyphens (-), underscores (_), dots (.), and spaces"
                ValidationResult.Error(message)
            }
            else -> ValidationResult.Success
        }
    }

    /**
     * The character-safety half of [validateContainerName], without the empty check.
     * Used to drop a container whose on-disk config name carries shell metacharacters
     * before it can reach a root command.
     */
    fun isSafeContainerName(name: String): Boolean =
        name.isNotEmpty() && name.matches(Regex("^[a-zA-Z0-9_\\s.-]+$"))

    /**
     * Validates hostname: only numbers, letters (lowercase and uppercase), and dashes allowed.
     * Empty is allowed (will use container name as default).
     */
    fun validateHostname(hostname: String, context: Context? = null): ValidationResult {
        return when {
            hostname.isEmpty() -> ValidationResult.Success // Empty is allowed
            !hostname.matches(Regex("^[a-zA-Z0-9-]+$")) -> {
                val message = context?.getString(R.string.error_hostname_invalid)
                    ?: "Hostname can only contain letters, numbers, and dashes (-)"
                ValidationResult.Error(message)
            }
            else -> ValidationResult.Success
        }
    }

    /**
     * Sanitizes a string (e.g. container name) to be a valid hostname.
     * Replaces spaces, underscores, and dots with dashes, removes other invalid characters, and trims dashes.
     */
    fun sanitizeHostname(name: String): String {
        return name.replace(Regex("[\\s_.]+"), "-")
            .replace(Regex("[^a-zA-Z0-9-]"), "")
            .trim('-')
    }

    /**
     * Key of a `KEY=VALUE` env-file line, null if the backend would skip it.
     * Mirrors parse_env_file_to_config() in src/config/parse.c: blank and `#`
     * lines are ignored, an `export ` prefix is tolerated, and the key must be
     * `[A-Za-z_][A-Za-z0-9_]*`.
     */
    fun envLineKey(line: String): String? {
        val l = line.trim().removePrefix("export ")
        if (l.isEmpty() || l.startsWith("#")) return null
        return ENV_LINE.matchEntire(l)?.groupValues?.get(1)
    }
    private val ENV_LINE = Regex("([A-Za-z_][A-Za-z0-9_]*)=.*")

    /**
     * Whether the backend will mount over [dest] inside the container. Mirrors
     * validate_bind_destination() in src/config/parse.c: absolute, at least one real
     * path segment (so not "/" or "//", which would cover the rootfs itself),
     * no "." or ".." segments, no control characters, shorter than PATH_MAX.
     * Also no ',' or ':', which the bind_mounts=src:dest[:ro],... line uses
     * as separators, so either would save a different mount than the one typed.
     */
    fun isValidBindDestination(dest: String): Boolean {
        if (!dest.startsWith("/") || dest.length >= 4096 || dest.any { it.isISOControl() }) return false
        if (!isBindSafe(dest)) return false
        val segments = dest.split('/').filter { it.isNotEmpty() }
        return segments.isNotEmpty() && segments.none { it == "." || it == ".." }
    }

    /** Whether [path] can sit in the bind_mounts line without being split apart. */
    fun isBindSafe(path: String): Boolean = path.none { it == ',' || it == ':' }

    /** Count the env-file lines the backend will actually apply. */
    fun countEnvVars(content: String?): Int =
        content?.lines()?.count { envLineKey(it) != null } ?: 0

    /**
     * Reject line breaks / control characters in the single-line container-config
     * values. Each is written as a `key=value` line into the root-owned
     * container.config parsed by the privileged backend, so a newline would inject
     * an extra trusted key. `envFileContent` is intentionally excluded, it is
     * legitimately multi-line and written to a separate `.env` file.
     */
    fun validateConfigValues(config: ContainerInfo): ValidationResult {
        fun hasControl(v: String) = v.any { it.isISOControl() }
        val invalid = listOf(
            config.dnsServers, config.staticNatIp, config.customInit,
            config.tx11ExtraFlags, config.virglExtraFlags, config.privileged,
            config.gatewayContainer, config.gatewayNet, config.gatewayIface, config.gatewayBridge
        ).any { hasControl(it) }
        return if (invalid) {
            ValidationResult.Error("Configuration values must not contain line breaks")
        } else {
            ValidationResult.Success
        }
    }

    // ---- Gateway networking mode --------------------------------------------

    // Linux IFNAMSIZ is 16 incl. NUL, so interface/bridge names get 15 usable chars.
    const val IFNAME_MAX = 15
    private val IFNAME_REGEX = Regex("^[a-zA-Z0-9_-]+$")

    /**
     * What a typed interface name may contain, cut at IFNAMSIZ-1 the way the
     * backend's ds_parse_iface_csv() and the kernel require: a longer name is
     * dropped at start with no error, and a ',' or ':' would split the config
     * line. [wildcards] admits the fnmatch() metacharacters that
     * resolve_pinned_uplink() in src/net/route/uplink.c matches an upstream list by.
     */
    fun ifaceNameInput(input: String, wildcards: Boolean = false): String =
        input.filter { it.isLetterOrDigit() || it in "_-." || (wildcards && it in "*?") }.take(IFNAME_MAX)

    /** Effective LAN segment name (empty -> "lan"), mirrors the C runtime default. */
    fun effGatewayNet(net: String): String = net.ifBlank { "lan" }

    /** Effective interface name inside the gateway (empty -> "eth1"). */
    fun effGatewayIface(iface: String): String = iface.ifBlank { "eth1" }

    /**
     * Effective host bridge name. Mirrors gateway_bridge_name() in src/net/naming.c:
     * explicit bridge wins, else "ds-" + (net filtered to [A-Za-z0-9_-], first 9 chars,
     * or "lan" if empty).
     */
    fun effGatewayBridge(net: String, bridge: String): String {
        if (bridge.isNotBlank()) return bridge
        val clean = effGatewayNet(net)
            .filter { it.isLetterOrDigit() || it == '_' || it == '-' }
            .take(9)
        return "ds-" + clean.ifEmpty { "lan" }
    }

    private fun ifaceLikeError(value: String, context: Context?): String? = when {
        value.isBlank() -> null
        value.length > IFNAME_MAX ->
            context?.getString(R.string.error_iface_too_long) ?: "Too long, max $IFNAME_MAX characters"
        !value.matches(IFNAME_REGEX) ->
            context?.getString(R.string.error_gateway_name_chars) ?: "Use only letters, digits, _ or -"
        else -> null
    }

    /**
     * Validates a gateway-mode configuration against every OTHER installed container,
     * returning a per-field error set. Enforces the collision rules from
     * Documentation/Networking-From-Zero.md (Part 10) so two segments never share a
     * host bridge and one gateway never reuses an interface name across segments.
     */
    fun validateGatewayConfig(
        selfName: String,
        gatewayContainer: String,
        net: String,
        iface: String,
        bridge: String,
        installed: List<ContainerInfo>,
        context: Context? = null
    ): GatewayErrors {
        // Field-format checks (independent of other containers).
        val netErr = if (net.isNotBlank() && !net.matches(IFNAME_REGEX))
            (context?.getString(R.string.error_gateway_name_chars) ?: "Use only letters, digits, _ or -")
        else null
        var ifaceErr = ifaceLikeError(iface, context)
        var bridgeErr = ifaceLikeError(bridge, context)

        // Gateway container must exist, differ from self, and be installed.
        val installedNames = installed.map { it.name }
        val containerErr = when {
            gatewayContainer.isBlank() ->
                context?.getString(R.string.error_gateway_required) ?: "Select a gateway container"
            gatewayContainer == selfName ->
                context?.getString(R.string.error_gateway_self) ?: "A container cannot be its own gateway"
            gatewayContainer !in installedNames ->
                context?.getString(R.string.error_gateway_not_installed) ?: "Gateway container is not installed"
            else -> null
        }

        // Cross-container collisions (only when the basics are already sound).
        if (containerErr == null && netErr == null && ifaceErr == null && bridgeErr == null) {
            val myBridge = effGatewayBridge(net, bridge)
            val myNet = effGatewayNet(net)
            val myIface = effGatewayIface(iface)
            for (o in installed) {
                if (o.name == selfName || o.netMode != "gateway" || o.gatewayContainer.isBlank()) continue
                val oBridge = effGatewayBridge(o.gatewayNet, o.gatewayBridge)
                val oNet = effGatewayNet(o.gatewayNet)
                val oIface = effGatewayIface(o.gatewayIface)
                // Same host bridge but a different gateway, or a different segment of the
                // same gateway -> two routers / two segments on one switch.
                if (oBridge == myBridge && (o.gatewayContainer != gatewayContainer || oNet != myNet)) {
                    bridgeErr = context?.getString(R.string.error_gateway_bridge_taken, o.name)
                        ?: "Bridge $myBridge is already used by '${o.name}'. Use a different LAN name."
                    break
                }
                // Same gateway, different segment, but the same interface name reused.
                if (o.gatewayContainer == gatewayContainer && oBridge != myBridge && oIface == myIface) {
                    ifaceErr = context?.getString(R.string.error_gateway_iface_taken, o.name)
                        ?: "Interface $myIface is already used by another segment ('${o.name}'). Use a different one."
                    break
                }
            }
        }

        return GatewayErrors(containerErr, netErr, ifaceErr, bridgeErr)
    }

    /**
     * Validates a macvlan-mode configuration. The parent is required. Sharing it
     * with another macvlan container is only a warning when either side is in
     * passthru mode: the two cannot run at the same time, but taking turns is
     * fine, and the backend refuses the second one with the reason if they try.
     */
    fun validateMacvlanConfig(
        selfName: String,
        netMode: String,
        parent: String,
        mode: String,
        installed: List<ContainerInfo>,
        context: Context? = null
    ): MacvlanErrors {
        if (netMode != "macvlan") return MacvlanErrors()
        if (parent.isBlank()) {
            return MacvlanErrors(
                parent = context?.getString(R.string.error_macvlan_parent_required) ?: "Choose the host interface"
            )
        }
        val clash = installed.firstOrNull {
            it.name != selfName && it.netMode == "macvlan" && it.macvlanParent == parent &&
                (it.macvlanMode == "passthru" || mode == "passthru")
        }
        return MacvlanErrors(
            warning = clash?.let {
                context?.getString(R.string.warning_macvlan_passthru_shared, parent, it.name)
                    ?: "$parent is also used by '${it.name}'. In passthru mode only one of them can run at a time."
            }
        )
    }

    /**
     * Validates a new port forward against the rules already in the list. Ports
     * are a single number or an `a-b` range, the container side defaults to the
     * host side, the two sides of a range must be the same width, and a rule may
     * not overlap an existing one of the same protocol on either side.
     */
    fun validatePortForward(
        hostPort: String,
        containerPort: String,
        proto: String,
        existing: List<PortForward>,
        context: Context
    ): PortForwardErrors {
        val hostErr = portSpecError(hostPort, context)
        val contErr = portSpecError(containerPort, context)
        if (hostErr != null || contErr != null || hostPort.isBlank()) return PortForwardErrors(hostErr, contErr)

        if (containerPort.isNotBlank() && rangeWidth(hostPort) != rangeWidth(containerPort)) {
            return PortForwardErrors(pair = context.getString(R.string.error_port_width_mismatch))
        }
        val newHost = parseRange(hostPort.trim())
        val newCont = parseRange(containerPort.ifBlank { hostPort }.trim())
        val overlap = existing.any { ex ->
            ex.proto == proto && (overlaps(newHost, parseRange(ex.hostPort)) ||
                overlaps(newCont, parseRange(ex.containerPort ?: ex.hostPort)))
        }
        return if (overlap) PortForwardErrors(pair = context.getString(R.string.error_port_overlap)) else PortForwardErrors()
    }

    private fun portSpecError(spec: String, context: Context): String? {
        if (spec.isBlank()) return null
        if (spec.contains("-")) {
            val parts = spec.split("-")
            if (parts.size != 2) return context.getString(R.string.error_invalid_range_format)
            val start = parts[0].toIntOrNull()
            val end = parts[1].toIntOrNull()
            if (start == null || end == null) return context.getString(R.string.error_ports_must_be_numbers)
            if (start !in 1..65535 || end !in 1..65535) return context.getString(R.string.error_port_out_of_range)
            if (start >= end) return context.getString(R.string.error_start_must_be_less_than_end)
            return null
        }
        val p = spec.toIntOrNull() ?: return context.getString(R.string.error_port_must_be_number)
        return if (p !in 1..65535) context.getString(R.string.error_port_out_of_range) else null
    }

    private fun rangeWidth(spec: String): Int = parseRange(spec).let { it.second - it.first }

    private fun parseRange(spec: String): Pair<Int, Int> {
        val parts = spec.split("-")
        val start = parts[0].toIntOrNull() ?: 0
        return start to (parts.getOrNull(1)?.toIntOrNull() ?: start)
    }

    private fun overlaps(a: Pair<Int, Int>, b: Pair<Int, Int>) = a.first <= b.second && b.first <= a.second
}

/**
 * Port-forward validation. [host] and [container] belong to their fields,
 * [pair] to the rule as a whole (width mismatch, overlap).
 */
data class PortForwardErrors(
    val host: String? = null,
    val container: String? = null,
    val pair: String? = null
) {
    val isValid: Boolean
        get() = host == null && container == null && pair == null
}

/** Macvlan-mode validation. [parent] blocks saving; [warning] does not. */
data class MacvlanErrors(
    val parent: String? = null,
    val warning: String? = null
) {
    val isValid: Boolean
        get() = parent == null
}

/**
 * Per-field validation result for gateway mode. A null field means "no error".
 */
data class GatewayErrors(
    val container: String? = null,
    val net: String? = null,
    val iface: String? = null,
    val bridge: String? = null
) {
    val isValid: Boolean
        get() = container == null && net == null && iface == null && bridge == null
}

/**
 * Sealed class for validation results - more type-safe than nullable strings.
 */
sealed class ValidationResult {
    object Success : ValidationResult()
    data class Error(val message: String) : ValidationResult()

    val isError: Boolean get() = this is Error
    val errorMessage: String? get() = (this as? Error)?.message
}

