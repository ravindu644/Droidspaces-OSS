package com.droidspaces.app.util

import android.content.Context
import com.droidspaces.app.R

/**
 * Centralized validation utilities to eliminate duplication.
 * All validation logic in one place for consistency and maintainability.
 */
object ValidationUtils {
    const val MAX_CONTAINER_NAME_LENGTH = 17  // 63 - len("/data/local/Droidspaces/Containers/") - len("/rootfs.img")

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
            name.length > MAX_CONTAINER_NAME_LENGTH -> {
                val message = context?.getString(R.string.error_container_name_too_long)
                    ?: "Name too long — max $MAX_CONTAINER_NAME_LENGTH characters"
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

    // ---- Gateway networking mode --------------------------------------------

    // Linux IFNAMSIZ is 16 incl. NUL, so interface/bridge names get 15 usable chars.
    const val IFNAME_MAX = 15
    private val IFNAME_REGEX = Regex("^[a-zA-Z0-9_-]+$")

    /** Effective LAN segment name (empty -> "lan"), mirrors the C runtime default. */
    fun effGatewayNet(net: String): String = net.ifBlank { "lan" }

    /** Effective interface name inside the gateway (empty -> "eth1"). */
    fun effGatewayIface(iface: String): String = iface.ifBlank { "eth1" }

    /**
     * Effective host bridge name. Mirrors gateway_bridge_name() in src/net/network.c:
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
            context?.getString(R.string.error_iface_too_long) ?: "Too long — max $IFNAME_MAX characters"
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

