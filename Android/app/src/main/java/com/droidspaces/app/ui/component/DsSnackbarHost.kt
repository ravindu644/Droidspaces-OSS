package com.droidspaces.app.ui.component

import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Snackbar
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.SwipeToDismissBox
import androidx.compose.material3.SwipeToDismissBoxValue
import androidx.compose.material3.rememberSwipeToDismissBoxState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.key
import androidx.compose.ui.Modifier

/**
 * The app's snackbar host: the stock material3 snackbar, but swipeable off the
 * screen in either direction instead of only waiting out the timeout.
 */
@OptIn(ExperimentalMaterial3Api::class) // SwipeToDismissBox, stable from m3 1.3
@Composable
fun DsSnackbarHost(hostState: SnackbarHostState, modifier: Modifier = Modifier) {
    SnackbarHost(hostState = hostState, modifier = modifier) { data ->
        // key(data): a fresh swipe state per message, so the next snackbar does
        // not inherit the dismissed position of the previous one
        key(data) {
            val dismissState = rememberSwipeToDismissBoxState()
            // Dismiss after the box settles off-screen, so the user sees the
            // full slide-out; confirmValueChange would cut it off mid-swipe
            if (dismissState.currentValue != SwipeToDismissBoxValue.Settled) {
                LaunchedEffect(Unit) { data.dismiss() }
            }
            SwipeToDismissBox(
                state = dismissState,
                backgroundContent = {},
            ) {
                Snackbar(data)
            }
        }
    }
}
