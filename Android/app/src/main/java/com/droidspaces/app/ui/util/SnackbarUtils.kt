package com.droidspaces.app.ui.util

import androidx.compose.material3.SnackbarDuration
import androidx.compose.material3.SnackbarHostState
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch

/**
 * Show success snackbar with short duration.
 */
suspend fun SnackbarHostState.showSuccess(message: String) {
    showSnackbar(
        message = message,
        duration = SnackbarDuration.Short
    )
}

/**
 * Show error snackbar with long duration.
 */
suspend fun SnackbarHostState.showError(message: String) {
    showSnackbar(
        message = message,
        duration = SnackbarDuration.Long
    )
}

/**
 * Show success snackbar from a coroutine scope.
 */
fun CoroutineScope.showSuccess(snackbarHostState: SnackbarHostState, message: String) {
    launch {
        snackbarHostState.showSuccess(message)
    }
}

/**
 * Show error snackbar from a coroutine scope.
 */
fun CoroutineScope.showError(snackbarHostState: SnackbarHostState, message: String) {
    launch {
        snackbarHostState.showError(message)
    }
}
