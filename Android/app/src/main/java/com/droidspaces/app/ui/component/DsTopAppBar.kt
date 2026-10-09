package com.droidspaces.app.ui.component

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.size
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Close
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExperimentalMaterial3ExpressiveApi
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarScrollBehavior
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.droidspaces.app.R

/**
 * The one app bar: the small TopAppBar at its 64.dp, the same on every screen so the
 * title and the first card land at the same y everywhere.
 *
 * Sub-screens pass [onBack] and get the back arrow, or a cross with [close] for a
 * flow the user abandons rather than steps out of. The tabs pass [glyph] instead,
 * which sits in the same 48.dp box the arrow does, so the title starts at one x
 * across the whole app. A screen with neither (an install in flight) gets no
 * leading icon.
 *
 * [titleStyle] exists for the unit name screens, whose titles are long machine text
 * and stay smaller by decision (DESIGN.md, Decided exceptions). Everything else keeps
 * titleLarge.
 */
@OptIn(ExperimentalMaterial3Api::class, ExperimentalMaterial3ExpressiveApi::class)
@Composable
fun DsTopAppBar(
    title: String,
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
    close: Boolean = false,
    glyph: ImageVector? = null,
    subtitle: String? = null,
    titleStyle: TextStyle = MaterialTheme.typography.titleLarge,
    scrollBehavior: TopAppBarScrollBehavior? = null,
    actions: @Composable RowScope.() -> Unit = {},
) {
    val context = LocalContext.current
    TopAppBar(
        title = { Text(title, style = titleStyle, maxLines = 1, overflow = TextOverflow.Ellipsis) },
        subtitle = {
            if (subtitle != null) {
                Text(
                    subtitle,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 1
                )
            }
        },
        modifier = modifier,
        navigationIcon = {
            when {
                onBack != null -> IconButton(onClick = onBack) {
                    if (close) Icon(Icons.Default.Close, context.getString(R.string.close))
                    else Icon(Icons.AutoMirrored.Filled.ArrowBack, context.getString(R.string.back))
                }
                glyph != null -> Box(Modifier.size(48.dp), contentAlignment = Alignment.Center) {
                    Icon(glyph, contentDescription = null)
                }
            }
        },
        actions = actions,
        scrollBehavior = scrollBehavior
    )
}
