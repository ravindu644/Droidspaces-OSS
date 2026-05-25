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

