package com.droidspaces.app.ui.component

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.OffsetMapping
import androidx.compose.ui.text.input.TransformedText
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.unit.dp
import com.droidspaces.app.R
import com.droidspaces.app.ui.theme.JetBrainsMono
import com.droidspaces.app.util.ValidationUtils

/**
 * The env file as typed, one KEY=VALUE per line, coloured so a bad line stands
 * out before saving. The text is handed back as typed, comments included,
 * because parse_env_file_to_config() already skips what it cannot parse; the
 * page only refuses to save while a line would be skipped.
 */
@Composable
fun EnvironmentVariablesPage(initial: String, onSave: (String) -> Unit, onClose: () -> Unit) {
    val context = LocalContext.current
    var text by rememberSaveable { mutableStateOf(initial) }
    val entries = text.lines().filter { it.isNotBlank() && !it.trim().startsWith("#") }
    val count = entries.count { ValidationUtils.envLineKey(it) != null }
    val skipped = entries.size - count
    val dirty = text.trim() != initial.trim()

    DraftPageScaffold(
        title = context.getString(R.string.environment_variables),
        dirty = dirty,
        canSave = skipped == 0,
        onSave = { onSave(text.trim()) },
        onClose = onClose,
        scrollable = false
    ) {
        val syntax = rememberEnvSyntax()
        OutlinedTextField(
            value = text,
            onValueChange = { text = it },
            modifier = Modifier
                .weight(1f)
                .fillMaxWidth()
                .padding(horizontal = screenGutter(), vertical = 8.dp),
            placeholder = {
                Text(
                    context.getString(R.string.env_placeholder),
                    style = MaterialTheme.typography.bodyMedium,
                    fontFamily = JetBrainsMono,
                    color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.4f)
                )
            },
            visualTransformation = syntax,
            shape = RoundedCornerShape(20.dp),
            colors = DsTextFieldDefaults.colors(),
            textStyle = MaterialTheme.typography.bodyMedium.copy(fontFamily = JetBrainsMono),
            keyboardOptions = KeyboardOptions(
                keyboardType = KeyboardType.Ascii,
                capitalization = KeyboardCapitalization.None,
                autoCorrectEnabled = false
            )
        )
        Column(
            modifier = Modifier.padding(start = screenGutter() + 16.dp, end = screenGutter() + 16.dp, bottom = 12.dp),
            verticalArrangement = Arrangement.spacedBy(4.dp)
        ) {
            Text(
                context.getString(R.string.environment_variables_configured, count),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
            )
            if (skipped > 0) {
                Text(context.getString(R.string.env_not_key_value), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.error)
            }
        }
    }
}

@Composable
private fun rememberEnvSyntax(): VisualTransformation {
    val scheme = MaterialTheme.colorScheme
    val dim = scheme.onSurfaceVariant.copy(alpha = 0.55f)
    return remember(scheme) { EnvSyntax(key = scheme.primary, dim = dim, quoted = scheme.tertiary, bad = scheme.error) }
}

/**
 * Colours an env file line by line: keys, the `=`, quoted values, `#` comments,
 * and lines the backend would skip. Only colour changes, so offsets map 1:1.
 */
private class EnvSyntax(
    private val key: Color,
    private val dim: Color,
    private val quoted: Color,
    private val bad: Color,
) : VisualTransformation {
    override fun filter(text: AnnotatedString): TransformedText {
        val src = text.text
        val styled = buildAnnotatedString {
            append(src)
            var start = 0
            for (line in src.split('\n')) {
                val end = start + line.length
                val trimmed = line.trim()
                val k = ValidationUtils.envLineKey(line)
                when {
                    trimmed.isEmpty() -> {}
                    trimmed.startsWith("#") -> addStyle(SpanStyle(color = dim, fontStyle = FontStyle.Italic), start, end)
                    k != null -> {
                        // envLineKey() trims and drops an "export " prefix, so the key
                        // starts right after those; indexOf would find "e" in "export".
                        val lead = line.length - line.trimStart().length
                        val ks = start + lead + (if (line.startsWith("export ", lead)) 7 else 0)
                        val eq = ks + k.length
                        addStyle(SpanStyle(color = key), ks, eq)
                        addStyle(SpanStyle(color = dim), eq, eq + 1)
                        val value = line.substring(eq + 1 - start).trim()
                        if (value.length >= 2 && value.first() in "\"'" && value.last() == value.first()) {
                            addStyle(SpanStyle(color = quoted), eq + 1, end)
                        }
                    }
                    else -> addStyle(SpanStyle(color = bad, textDecoration = TextDecoration.Underline), start, end)
                }
                start = end + 1
            }
        }
        return TransformedText(styled, OffsetMapping.Identity)
    }
}
