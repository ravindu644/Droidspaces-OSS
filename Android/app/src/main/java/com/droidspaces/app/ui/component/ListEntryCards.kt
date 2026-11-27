package com.droidspaces.app.ui.component

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.ui.draw.clip
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Surface
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import com.droidspaces.app.util.AnimationUtils
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.remember
import androidx.compose.runtime.getValue
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.animation.shrinkVertically
import androidx.compose.animation.fadeOut
import androidx.compose.animation.core.MutableTransitionState
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.runtime.compositionLocalOf
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.text.style.TextOverflow
import com.droidspaces.app.ui.theme.JetBrainsMono
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp

/*
 * The two pieces every editable list in the container config is made of: an
 * entry with a delete button, and the add row under the entries. Upstream
 * interfaces, port forwards, the macvlan parent and bind mounts all use these,
 * so they cannot drift apart again.
 */

/**
 * One entry of an editable list inside a [SettingsGroup]: flat, so the group's
 * surface is the only card. [leading] is for an icon or a drag handle,
 * [supporting] a quieter second line. Entries are machine values (interface
 * names, ports, paths), so the text is monospace.
 */
@Composable
fun EntryRow(
    text: String,
    onDelete: () -> Unit,
    deleteDescription: String,
    modifier: Modifier = Modifier,
    supporting: String? = null,
    leading: (@Composable RowScope.() -> Unit)? = null,
    trailingBadge: (@Composable RowScope.() -> Unit)? = null,
) {
    Row(
        modifier = modifier
            .fillMaxWidth()
            // The card these replaced: 12dp around the 48dp delete button.
            .heightIn(min = 72.dp)
            .padding(start = 16.dp, end = 4.dp, top = 12.dp, bottom = 12.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        if (leading != null) {
            leading()
            Spacer(Modifier.width(16.dp))
        }
        Column(Modifier.weight(1f)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(
                    text = text,
                    style = MaterialTheme.typography.bodyLarge,
                    fontFamily = JetBrainsMono,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.weight(1f, fill = false)
                )
                trailingBadge?.invoke(this)
            }
            if (supporting != null) {
                Text(
                    supporting,
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f),
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis
                )
            }
        }
        IconButton(onClick = onDelete) {
            Icon(Icons.Default.Delete, contentDescription = deleteDescription, tint = MaterialTheme.colorScheme.error)
        }
    }
}

/** The "add" button under an editable list, the card button the form has always used. */
@Composable
fun AddEntryButton(label: String, onClick: () -> Unit, modifier: Modifier = Modifier, enabled: Boolean = true) {
    val shape = RoundedCornerShape(16.dp)
    Surface(
        modifier = modifier.fillMaxWidth().clip(shape).clickable(enabled = enabled, onClick = onClick).alpha(if (enabled) 1f else 0.38f),
        shape = shape,
        color = MaterialTheme.colorScheme.surfaceContainerLow,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f)),
        tonalElevation = 0.dp
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 14.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.Center
        ) {
            Icon(Icons.Default.Add, contentDescription = null, modifier = Modifier.size(18.dp), tint = MaterialTheme.colorScheme.onSurfaceVariant)
            Spacer(modifier = Modifier.width(8.dp))
            Text(
                label,
                style = MaterialTheme.typography.labelLarge,
                fontWeight = FontWeight.SemiBold,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis
            )
        }
    }
}

/**
 * True once an [EntryGroup] has drawn its first frame. Entries composed before
 * that were already in the list when the page opened and appear in place;
 * entries composed after it were just added and expand in.
 */
internal val LocalEntryGroupSettled = compositionLocalOf { false }

/**
 * Lets a list entry come and go without the rows below it jumping: an entry
 * added to a settled [EntryGroup] expands in, and [content] gets a remove
 * function that shrinks and fades the entry first, with [onRemoved] taking
 * it out of the list once that has played. Key the entry (`key(item) { }`)
 * so the state stays with it, not with its position.
 */
@Composable
fun AnimatedRemoval(onRemoved: () -> Unit, content: @Composable (remove: () -> Unit) -> Unit) {
    val added = LocalEntryGroupSettled.current
    val visible = remember { MutableTransitionState(!added).apply { targetState = true } }
    val removed by rememberUpdatedState(onRemoved)
    LaunchedEffect(visible.isIdle, visible.targetState) {
        if (!visible.targetState && visible.isIdle) removed()
    }
    AnimatedVisibility(
        visibleState = visible,
        enter = expandVertically(AnimationUtils.fastSpec()) + fadeIn(AnimationUtils.fastSpec()),
        exit = shrinkVertically(AnimationUtils.fastSpec()) + fadeOut(AnimationUtils.fastSpec())
    ) {
        content { visible.targetState = false }
    }
}
