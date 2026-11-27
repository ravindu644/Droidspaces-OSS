package com.droidspaces.app.ui.component

import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.ui.unit.dp

/**
 * Card headers line up across tabs. The status card on the home tab, the container
 * card, the running container card and the init service row all put a title on the
 * left and a status pill on the right above a divider, and a user switching tabs
 * sees those dividers and pills hold the same line.
 *
 * That only works while all four use these two values, so they live here rather
 * than being retyped in four files. Changing one of them moves every card header
 * in the app, which is the point: it cannot be changed for one card by accident.
 *
 * The header height is 48.dp because the container card puts an IconButton in it
 * and that is the minimum touch target.
 */
val CardContentPadding = PaddingValues(horizontal = 16.dp, vertical = 12.dp)

/** Height of the title-and-pill row at the top of a card. See [CardContentPadding]. */
val CardHeaderHeight = 48.dp
