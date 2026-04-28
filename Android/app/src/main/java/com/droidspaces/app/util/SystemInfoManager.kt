package com.droidspaces.app.util

import android.content.Context
import android.os.Build
import android.system.Os
import com.topjohnwu.superuser.Shell
import kotlinx.coroutines.async
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import com.droidspaces.app.util.DroidspacesChecker

/**
 * High-performance system information manager.
 * Loads all system info in parallel on boot for instant access.
 * Uses caching to avoid redundant operations.
 */
object SystemInfoManager {
    // Cache static system info (never changes during app lifetime)
    private val unameCache by lazy { Os.uname() }

    // Cache mutable info (loaded once on boot)
    // Use @Volatile for thread-safe access without synchronization overhead
    @Volatile
    private var rootProviderVersionCache: String? = null

    @Volatile
    private var selinuxStatusCache: String? = null

    @Volatile
    private var droidspacesVersionCache: String? = null

    @Volatile
    private var backendModeCache: String? = null

    // Expose cache for direct access (zero-overhead)
    // Public access for UI components to show cached value immediately
    val cachedSelinuxStatus: String? get() = selinuxStatusCache

    @Volatile
    private var isInitialized = false

    // Context for accessing preferences (set during initialization)
    @Volatile
    private var context: Context? = null

    // Static info - cached to avoid string allocations
    val kernelVersion: String get() = unameCache.release
    val architecture: String get() = unameCache.machine

    // Cache androidVersion string (computed once, reused forever - saves ~50-100ns per access)
    private val androidVersionCache by lazy {
        "${Build.VERSION.RELEASE} (${Build.VERSION.SDK_INT})"
    }
    val androidVersion: String get() = androidVersionCache

    // Manager version is constant - no allocation needed
    val managerVersion: String get() = "1.0.0 (1)"

    /**
     * Initialize all system info in parallel on boot.
     * Optimized to minimize I/O operations and cache aggressively.
     *
     * Performance: ~50-100ms on first launch, ~0ms on subsequent launches (cached)
     */
    suspend fun initialize(appContext: Context? = null) = withContext(Dispatchers.IO) {
        // Fast path: already initialized and no new context
        if (isInitialized && appContext == null) return@withContext

        // Store context for preference access (only if provided)
        val appContextSafe = appContext?.applicationContext
        if (appContextSafe != null) {
            context = appContextSafe
        }

        // Use local variable to avoid repeated null checks
        val ctx = context ?: return@withContext

        coroutineScope {
            // Load cached values first (instant access, ~0.1ms)
            val cachedRootVersion = PreferencesManager.getInstance(ctx).cachedRootProviderVersion
            val cachedDroidspacesVersion = PreferencesManager.getInstance(ctx).cachedDroidspacesVersion

            // Load root provider, SELinux status, and droidspaces version in parallel (non-blocking)
            val rootProviderDeferred = async { loadRootProviderVersion() }
            val selinuxDeferred = async { SELinuxChecker.getSELinuxStatus() }
            val droidspacesVersionDeferred = async { DroidspacesChecker.getDroidspacesVersion() }

            // Wait for all to complete (parallel execution)
            val newRootVersion = rootProviderDeferred.await()
            rootProviderVersionCache = newRootVersion
            selinuxStatusCache = selinuxDeferred.await()
            val newDroidspacesVersion = droidspacesVersionDeferred.await()
            droidspacesVersionCache = newDroidspacesVersion

            // Fetch backend mode securely in background
            val newBackendMode = withContext(Dispatchers.IO) {
                try {
                    val cmd = Constants.getDroidspacesCommand()
                    val result = Shell.cmd("$cmd mode 2>/dev/null").exec()
                    if (result.isSuccess && result.out.isNotEmpty()) {
                        result.out[0].trim().uppercase()
                    } else null
                } catch (e: Exception) { null }
            }
            backendModeCache = newBackendMode

            // Save to preferences if changed (async write, non-blocking)
            if (newRootVersion != cachedRootVersion && newRootVersion != "Unknown") {
                PreferencesManager.getInstance(ctx).cachedRootProviderVersion = newRootVersion
            }
            if (newDroidspacesVersion != cachedDroidspacesVersion && newDroidspacesVersion != null) {
                PreferencesManager.getInstance(ctx).cachedDroidspacesVersion = newDroidspacesVersion
            }
            if (newBackendMode != null) {
                PreferencesManager.getInstance(ctx).cachedBackendMode = newBackendMode
            }

            isInitialized = true
        }
    }

    /**
     * Get cached root provider version synchronously (for instant UI display).
     * Optimized hot path - direct property access.
     *
     * Performance: ~0.1ms (SharedPreferences cached read)
     */
    fun getCachedRootProviderVersion(context: Context): String? {
        return PreferencesManager.getInstance(context).cachedRootProviderVersion
    }

    /**
     * Get root provider version (cached after initialization).
     * Optimized to avoid redundant I/O operations.
     *
     * Performance: ~0.1ms if cached, ~50-100ms if not cached
     */
    suspend fun getRootProviderVersion(context: Context? = null): String = withContext(Dispatchers.IO) {
        // Update context if provided (only once)
        val appContext = context?.applicationContext
        if (appContext != null) {
            this@SystemInfoManager.context = appContext
        }

        // Fast path: return cached value immediately
        val cached = rootProviderVersionCache
        if (cached != null && isInitialized) {
            return@withContext cached
        }

        // Initialize if not done yet
        if (!isInitialized) {
            initialize(context)
            // Return cached value after initialization
            return@withContext rootProviderVersionCache ?: "Unknown"
        }

        // Fallback: load if cache is null (shouldn't happen after init)
        val newVersion = loadRootProviderVersion().also {
            rootProviderVersionCache = it
        }

        // Save to preferences if we have context and value changed
        val ctx = this@SystemInfoManager.context
        if (ctx != null && newVersion != "Unknown") {
            val prefsManager = PreferencesManager.getInstance(ctx)
            if (prefsManager.cachedRootProviderVersion != newVersion) {
                prefsManager.cachedRootProviderVersion = newVersion
            }
        }

        newVersion
    }

    /**
     * Get SELinux status (cached after initialization).
     * Fast path returns cached value immediately.
     *
     * Performance: ~0.1ms if cached, ~10-20ms if not cached
     */
    suspend fun getSELinuxStatus(): String = withContext(Dispatchers.IO) {
        // Fast path: return cached value immediately
        val cached = selinuxStatusCache
        if (cached != null && isInitialized) {
            return@withContext cached
        }

        // Initialize if not done yet
        if (!isInitialized) {
            initialize()
            // Return cached value after initialization
            return@withContext selinuxStatusCache ?: "Unknown"
        }

        // Fallback: load if cache is null (shouldn't happen after init)
        SELinuxChecker.getSELinuxStatus().also {
            selinuxStatusCache = it
        }
    }

    /**
     * Synchronous SELinux status from cache.
     */
    fun getSELinuxStatusSync(): String {
        return selinuxStatusCache ?: "ENFORCING"
    }

    /**
     * Force refresh SELinux status (bypasses cache).
     * Use this when the user manually refreshes to get the latest status.
     *
     * Performance: ~10-20ms (shell command execution)
     */
    suspend fun refreshSELinuxStatus(): String = withContext(Dispatchers.IO) {
        SELinuxChecker.getSELinuxStatus().also {
            selinuxStatusCache = it
        }
    }

    /**
     * Load root provider version - optimized with minimal allocations
     */
    private suspend fun loadRootProviderVersion(): String = withContext(Dispatchers.IO) {
        return@withContext try {
            val result = Shell.cmd("su -v").exec()
            if (result.isSuccess && result.out.isNotEmpty()) {
                result.out[0].trim()
            } else {
                "Unknown"
            }
        } catch (e: Exception) {
            "Unknown"
        }
    }

    /**
     * Get cached droidspaces version synchronously (for instant UI display).
     * Optimized hot path - direct property access.
     *
     * Performance: ~0.1ms (SharedPreferences cached read)
     */
    fun getCachedDroidspacesVersion(context: Context): String? {
        return PreferencesManager.getInstance(context).cachedDroidspacesVersion
    }

    /**
     * Get droidspaces version (cached after initialization).
     * Optimized to avoid redundant I/O operations.
     *
     * Performance: ~0.1ms if cached, ~10-50ms if not cached
     */
    suspend fun getDroidspacesVersion(context: Context? = null): String? = withContext(Dispatchers.IO) {
        // Update context if provided (only once)
        val appContext = context?.applicationContext
        if (appContext != null) {
            this@SystemInfoManager.context = appContext
        }

        // Fast path: return cached value immediately
        val cached = droidspacesVersionCache
        if (cached != null && isInitialized) {
            return@withContext cached
        }

        // Initialize if not done yet
        if (!isInitialized) {
            initialize(context)
            // Return cached value after initialization
            return@withContext droidspacesVersionCache
        }

        // Fallback: load if cache is null (shouldn't happen after init)
        val newVersion = DroidspacesChecker.getDroidspacesVersion().also {
            droidspacesVersionCache = it
        }

        // Save to preferences if we have context and value changed
        val ctx = this@SystemInfoManager.context
        if (ctx != null && newVersion != null) {
            val prefsManager = PreferencesManager.getInstance(ctx)
            if (prefsManager.cachedDroidspacesVersion != newVersion) {
                prefsManager.cachedDroidspacesVersion = newVersion
            }
        }

        newVersion
    }

    /**
     * Force refresh droidspaces version (bypasses cache).
     * Use this after backend installation/update to get the latest version.
     *
     * Performance: ~10-50ms (shell command execution)
     */
    suspend fun refreshDroidspacesVersion(context: Context? = null): String? = withContext(Dispatchers.IO) {
        // Update context if provided
        val appContext = context?.applicationContext
        if (appContext != null) {
            this@SystemInfoManager.context = appContext
        }

        // Force fetch new version (bypass cache)
        val newVersion = DroidspacesChecker.getDroidspacesVersion().also {
            droidspacesVersionCache = it
        }

        // Save to preferences if we have context
        val ctx = this@SystemInfoManager.context
        if (ctx != null && newVersion != null) {
            val prefsManager = PreferencesManager.getInstance(ctx)
            prefsManager.cachedDroidspacesVersion = newVersion
        }

        newVersion
    }

    /**
     * Get cached backend mode synchronously.
     */
    fun getCachedBackendMode(context: Context): String? {
        return PreferencesManager.getInstance(context).cachedBackendMode
    }

    /**
     * Get backend mode (DAEMON/DIRECT) asynchronously.
     */
    suspend fun getBackendMode(context: Context? = null): String? = withContext(Dispatchers.IO) {
        // Fast path: return cached value immediately
        val cached = backendModeCache
        if (cached != null && isInitialized) {
            return@withContext cached
        }

        val result = try {
            val cmd = Constants.getDroidspacesCommand()
            val shellResult = Shell.cmd("$cmd mode 2>/dev/null").exec()
            if (shellResult.isSuccess && shellResult.out.isNotEmpty()) {
                shellResult.out[0].trim().uppercase()
            } else null
        } catch (e: Exception) { null }

        if (result != null) {
            backendModeCache = result
            context?.let {
                PreferencesManager.getInstance(it).cachedBackendMode = result
            }
        }
        result
    }

    /**
     * Force refresh backend mode (bypasses cache).
     */
    suspend fun refreshBackendMode(context: Context? = null): String? = withContext(Dispatchers.IO) {
        val result = try {
            val cmd = Constants.getDroidspacesCommand()
            val shellResult = Shell.cmd("$cmd mode 2>/dev/null").exec()
            if (shellResult.isSuccess && shellResult.out.isNotEmpty()) {
                shellResult.out[0].trim().uppercase()
            } else null
        } catch (e: Exception) { null }

        if (result != null) {
            backendModeCache = result
            context?.let {
                PreferencesManager.getInstance(it).cachedBackendMode = result
            }
        }
        result
    }

    /**
     * Reset Droidspaces-specific cache.
     * Called after installation/update to force refresh of version and mode.
     * Preserves system-level info (SELinux, Root Provider) to prevent UI flicker.
     */
    fun resetCache() {
        droidspacesVersionCache = null
        backendModeCache = null
        isInitialized = false
    }
}

