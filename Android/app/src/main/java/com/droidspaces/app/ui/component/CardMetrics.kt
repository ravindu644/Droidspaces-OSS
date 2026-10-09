package com.droidspaces.app.ui.component

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

/**
 * Card headers line up across tabs. The status card on Home, the container card, the
 * running container card and the init service row all put a 44.dp header first (a
 * 24.dp icon or a 44.dp icon tile, the title, a chip on the right) above a divider,
 * and a user swiping between tabs sees those dividers and top edges hold one line.
 *
 * That only works while all four use these two values, so they live here rather than
 * being retyped in four files. Changing one moves every card header in the app, which
 * is the point: it cannot be changed for one card by accident.
 *
 * 44 is below the 48.dp touch floor on purpose. An IconButton in the header still
 * gets its full 48.dp target: minimumInteractiveComponentSize lets the hit area
 * overhang the row rather than growing it.
 */
val CardContentPadding = PaddingValues(16.dp)

/** Height of the title-and-chip row at the top of a card. See [CardContentPadding]. */
val CardHeaderHeight = 44.dp

/**
 * Cards are tonal, never bordered, except on AMOLED: there surfaceContainer and the
 * background are the same black and a card would have no edge at all. Then, and only
 * then, a card gets the 1px outlineVariant edge DESIGN.md gives boxes and group rows.
 */
@Composable
fun amoledCardBorder(): BorderStroke? {
    val scheme = MaterialTheme.colorScheme
    return if (scheme.surfaceContainer == scheme.background) BorderStroke(Dp.Hairline, scheme.outlineVariant) else null
}
