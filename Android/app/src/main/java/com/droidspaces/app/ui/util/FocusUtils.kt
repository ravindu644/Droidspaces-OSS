package com.droidspaces.app.ui.util

import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.text.input.ImeAction

/**
 * Centralized focus management utilities.
 */
object FocusUtils {
    /**
     * Keyboard actions that clear focus when done is pressed.
     */
    @Composable
    fun clearFocusKeyboardActions(): KeyboardActions {
        val focusManager = LocalFocusManager.current
        val keyboardController = LocalSoftwareKeyboardController.current
        val clear = {
            focusManager.clearFocus()
            keyboardController?.hide()
        }
        return KeyboardActions(onDone = { clear() }, onNext = { clear() }, onGo = { clear() }, onSearch = { clear() }, onSend = { clear() })
    }

    /**
     * Keyboard options with ImeAction.Done for single-line text fields.
     */
    val doneKeyboardOptions = KeyboardOptions(imeAction = ImeAction.Done)

    /**
     * Keyboard options with ImeAction.Search for search fields.
     */
    val searchKeyboardOptions = KeyboardOptions(imeAction = ImeAction.Search)
}

/**
 * Composable that provides a clearFocus function that can be called from non-composable contexts.
 */
@Composable
fun rememberClearFocus(): () -> Unit {
    val focusManager = LocalFocusManager.current
    val keyboardController = LocalSoftwareKeyboardController.current
    return {
        focusManager.clearFocus()
        keyboardController?.hide()
    }
}

/**
 * Box composable that clears focus when clicked outside.
 */
@Composable
fun ClearFocusOnClickOutside(
    modifier: Modifier = Modifier,
    content: @Composable () -> Unit
) {
    val focusManager = LocalFocusManager.current
    val keyboardController = LocalSoftwareKeyboardController.current

    Box(
        modifier = modifier
            .fillMaxSize()
            .clickable(
                indication = null,
                interactionSource = remember { MutableInteractionSource() }
            ) {
                focusManager.clearFocus()
                keyboardController?.hide()
            }
    ) {
        content()
    }
}

