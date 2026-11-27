package com.droidspaces.app.ui.component

import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.droidspaces.app.ui.theme.JetBrainsMono
import com.droidspaces.app.ui.util.FocusUtils

/**
 * The single-line field for a value the backend reads verbatim: a path, an
 * interface name, a flag string, a DNS list. Monospace so the user sees exactly
 * what will land in container.config, an ASCII keyboard with autocorrect off for
 * the same reason, and Done clears focus. Every form field of this kind goes
 * through here; the number fields and the env editor are different fields.
 */
@Composable
fun MonoField(
    value: String,
    onValueChange: (String) -> Unit,
    label: String,
    modifier: Modifier = Modifier,
    placeholder: String? = null,
    supporting: String? = null,
    isError: Boolean = false,
    leadingIcon: ImageVector? = null,
    keyboardType: KeyboardType = KeyboardType.Ascii,
    imeAction: ImeAction = ImeAction.Done,
) {
    OutlinedTextField(
        value = value,
        onValueChange = onValueChange,
        label = { Text(label, maxLines = 1, overflow = TextOverflow.Ellipsis) },
        placeholder = placeholder?.let { { Text(it, fontFamily = JetBrainsMono, maxLines = 1, overflow = TextOverflow.Ellipsis) } },
        supportingText = supporting?.let { { Text(it) } },
        isError = isError,
        leadingIcon = leadingIcon?.let { { Icon(it, contentDescription = null) } },
        singleLine = true,
        textStyle = MaterialTheme.typography.bodyLarge.copy(fontFamily = JetBrainsMono),
        shape = RoundedCornerShape(16.dp),
        colors = DsTextFieldDefaults.colors(),
        keyboardOptions = KeyboardOptions(keyboardType = keyboardType, imeAction = imeAction, autoCorrectEnabled = false),
        keyboardActions = if (imeAction == ImeAction.Done) FocusUtils.clearFocusKeyboardActions() else KeyboardActions.Default,
        modifier = modifier.fillMaxWidth()
    )
}
