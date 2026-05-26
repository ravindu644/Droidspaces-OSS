package com.droidspaces.app.nativebridge

/**
 * JNI bridge to the native droidspaces library.
 *
 * Rootless mode: all lifecycle + management operations run in-process
 * (no root needed, uses user namespaces).
 *
 * Root mode: lifecycle operations (start/stop/restart/enter) require UID 0
 * and use the companion runner binary via su. Info queries (version, mode)
 * and runInContainer work in-process regardless of mode.
 *
 * The native .so is loaded once and all calls are direct JNI -> C.
 */
object NativeBridge {

    init {
        System.loadLibrary("droidspaces")
    }

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    /**
     * Start a container.
     *
     * @param configPath absolute path to the KEY=VALUE config file
     * @param workspace  app-private workspace directory
     * @param rootless   true to enable rootless (user namespace) mode
     * @return PID of the monitor process, or -1 on failure
     */
    external fun startContainer(
        configPath: String,
        workspace: String,
        rootless: Boolean
    ): Int

    /**
     * Stop a container by name.
     *
     * @param name      container name
     * @param workspace app-private workspace directory
     * @param pid       monitor PID, or -1 for auto-detect
     * @return 0 on success, -1 on failure
     */
    external fun stopContainer(
        name: String,
        workspace: String,
        pid: Int
    ): Int

    /**
     * Restart a container.
     *
     * @param configPath absolute path to the KEY=VALUE config file
     * @param workspace  app-private workspace directory
     * @param rootless   true if rootless mode
     * @return 0 on success, -1 on failure
     */
    external fun restartContainer(
        configPath: String,
        workspace: String,
        rootless: Boolean
    ): Int

    // ── Status ────────────────────────────────────────────────────────────────

    /**
     * Get the PID of a running container.
     *
     * @return PID if running, -1 if stopped, -2 on error
     */
    external fun getContainerPid(
        name: String,
        workspace: String
    ): Int

    /**
     * Get container resource usage (UPTIME, RAM, CPU as key=value lines).
     */
    external fun getContainerUsage(
        name: String,
        workspace: String
    ): String

    // ─── Info ─────────────────────────────────────────────────────────────────

    /** Get the Droidspaces version string. */
    external fun getVersion(): String

    /** Get backend mode: "daemon" or "direct". */
    external fun getBackendMode(): String

    // ─── Run ──────────────────────────────────────────────────────────────────

    /**
     * Execute a command inside a running container and capture its output.
     *
     * @param name      container name
     * @param workspace app-private workspace directory
     * @param command   shell command to execute
     * @return stdout+stderr output of the command
     */
    external fun runInContainer(
        name: String,
        workspace: String,
        command: String
    ): String

    // ─── Enter ────────────────────────────────────────────────────────────────

    /**
     * Enter a container namespace and exec a shell (rootless mode).
     * Forks a child process; returns its PID.
     *
     * @param name      container name
     * @param workspace app-private workspace directory
     * @param user      user to enter as (e.g. "root"), or null/empty for default
     * @return child PID, or -1 on failure
     */
    external fun enterContainer(
        name: String,
        workspace: String,
        user: String
    ): Int
}
