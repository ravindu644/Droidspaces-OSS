package com.droidspaces.app.ui.theme

import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Shapes
import androidx.compose.ui.unit.dp

/*
 * The Expressive scale from DESIGN.md. Three slots move off the Material defaults:
 * extraSmall 6 (segmented group inner corners, default 4), medium 14 (the card icon
 * tile, default 12) and extraLarge 24 (cards and group outer corners, default 28).
 * The increased slots keep their defaults.
 */
val Shapes = Shapes(
    extraSmall = RoundedCornerShape(6.dp),
    small = RoundedCornerShape(8.dp),
    medium = RoundedCornerShape(14.dp),
    large = RoundedCornerShape(16.dp),
    extraLarge = RoundedCornerShape(24.dp),
)
