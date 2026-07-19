package com.droidspaces.app.util

/**
 * Shared safety choke point for the per-init-system service managers
 * ([ContainerSystemdManager], [ContainerOpenRCManager], [ContainerProcdManager]).
 *
 * Service/unit names are DISCOVERED from inside the container — which may be a
 * hostile third-party rootfs — and then interpolated into a host-level command:
 *
 *   droidspaces --name=<container> run '<init-tool> <action> <serviceName> ...'
 *
 * that executes in the app's real-host-root libsu shell. Because the name sits
 * inside the single-quoted `run '...'` payload, a name containing a single quote
 * (or `$`, `;`, backtick, whitespace, …) would break out of that payload and run
 * arbitrary commands as host root on a single UI tap (see FINDINGS_APP_VULN V1/V2).
 *
 * Every service manager MUST pass a name through [isSafeServiceName] before
 * building a command. The allow-list covers exactly the characters real
 * systemd / OpenRC / procd service names use and excludes every shell
 * metacharacter, so a validated name can be embedded literally and safely.
 */
object ServiceManagerBase {
    // Letters, digits, and the punctuation init systems actually allow in unit /
    // service names (systemd templates use '@', some units use ':'). NO quotes,
    // '$', ';', '|', '&', backtick, whitespace, parentheses or newlines.
    private val SAFE_SERVICE_NAME = Regex("^[A-Za-z0-9_.@:+-]+$")

    fun isSafeServiceName(name: String): Boolean = SAFE_SERVICE_NAME.matches(name)
}
