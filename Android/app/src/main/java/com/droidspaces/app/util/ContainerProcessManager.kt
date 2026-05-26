package com.droidspaces.app.util

import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * Manages process operations inside containers.
 * Executes commands using droidspaces run.
 */
object ContainerProcessManager {
    private const val TAG = "ContainerProcessManager"

    /**
     * Process information from ps output.
     */
    data class ProcessInfo(
        val pid: Int,
        val user: String?,
        val cpu: Double?,
        val mem: Double?,
        val command: String
    )

    /**
     * Get process list from container.
     * Uses 'ps -aux' or 'ps w' (for busybox).
     * Filters out our own ps command to avoid showing it in the list.
     */
    suspend fun getProcessList(containerName: String, rootless: Boolean = false): List<ProcessInfo> = withContext(Dispatchers.IO) {
        try {
            val output = ContainerRuntime.runInContainer(containerName, rootless,
                "ps -eo pid,user,%cpu,%mem,comm,args 2>/dev/null | grep -v \"ps -eo\" | grep -v \"grep\" || ps -aux 2>/dev/null | grep -v \"ps -aux\" | grep -v \"grep\" || ps w 2>/dev/null | grep -v \"ps w\" | grep -v \"grep\""
            )

            if (output.isEmpty() || output.startsWith("ERROR:")) {
                Log.w(TAG, "Failed to get process list for $containerName")
                return@withContext emptyList()
            }

            val lines = output.lines()
            // Filter out processes that are our own ps/grep commands
            val filteredOutput = lines.filter { line ->
                val trimmed = line.trim().lowercase()
                !trimmed.contains("ps -eo") &&
                !trimmed.contains("ps -aux") &&
                !trimmed.contains("ps w") &&
                !trimmed.contains("grep -v") &&
                !trimmed.contains("command -v")
            }

            parsePsOutput(filteredOutput)
        } catch (e: Exception) {
            Log.e(TAG, "Error getting process list for $containerName", e)
            emptyList()
        }
    }

    /**
     * Kill a process in container.
     */
    suspend fun killProcess(containerName: String, pid: Int, rootless: Boolean = false): Boolean = withContext(Dispatchers.IO) {
        try {
            val output = ContainerRuntime.runInContainer(containerName, rootless, "kill $pid")
            !output.startsWith("ERROR:")
        } catch (e: Exception) {
            Log.e(TAG, "Error killing process $pid in $containerName", e)
            false
        }
    }

    /**
     * Parse ps output into ProcessInfo list.
     * Handles both 'ps -aux' and 'ps w' formats.
     */
    private fun parsePsOutput(output: List<String>): List<ProcessInfo> {
        if (output.isEmpty()) return emptyList()

        val processes = mutableListOf<ProcessInfo>()
        val headerLine = output.firstOrNull() ?: return emptyList()

        // Find column indices
        val parts = headerLine.trim().split(Regex("\\s+"))
        val pidIdx = parts.indexOfOrNull("PID") ?: parts.indexOfOrNull("PID")
        val userIdx = parts.indexOfOrNull("USER")
        val cpuIdx = parts.indexOfOrNull("%CPU")
        val memIdx = parts.indexOfOrNull("%MEM")
        val cmdIdx = parts.indexOfOrNull("COMMAND") ?: parts.indexOfOrNull("CMD") ?: (parts.size - 1)

        // Parse each process line
        for (i in 1 until output.size) {
            val line = output[i].trim()
            if (line.isEmpty()) continue

            try {
                val lineParts = line.split(Regex("\\s+"))
                if (lineParts.size < 3) continue

                // PID is usually first or at pidIdx
                val pid = if (pidIdx != null && pidIdx < lineParts.size) {
                    lineParts[pidIdx].toIntOrNull()
                } else {
                    lineParts[0].toIntOrNull()
                } ?: continue

                // User
                val user = if (userIdx != null && userIdx < lineParts.size) {
                    lineParts[userIdx]
                } else if (lineParts.size > 1) {
                    lineParts[1]
                } else null

                // CPU
                val cpu = if (cpuIdx != null && cpuIdx < lineParts.size) {
                    lineParts[cpuIdx].toDoubleOrNull()
                } else null

                // Memory
                val mem = if (memIdx != null && memIdx < lineParts.size) {
                    lineParts[memIdx].toDoubleOrNull()
                } else null

                // Command (everything from cmdIdx onwards)
                val command = if (cmdIdx < lineParts.size) {
                    lineParts.subList(cmdIdx, lineParts.size).joinToString(" ")
                } else {
                    lineParts.lastOrNull() ?: ""
                }

                processes.add(ProcessInfo(
                    pid = pid,
                    user = user,
                    cpu = cpu,
                    mem = mem,
                    command = command
                ))
            } catch (e: Exception) {
                Log.w(TAG, "Failed to parse process line: $line", e)
            }
        }

        return processes
    }

    private fun List<String>.indexOfOrNull(element: String): Int? {
        val index = indexOf(element)
        return if (index >= 0) index else null
    }
}

