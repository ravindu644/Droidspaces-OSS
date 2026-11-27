package com.droidspaces.app.ui.theme

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.graphics.vector.addPathNodes
import androidx.compose.ui.graphics.vector.group
import androidx.compose.ui.unit.dp

/**
 * The app's own mark, the three bars of the launcher icon, as a 24dp glyph that
 * tints like a Material icon. Every place the UI showed a disk for a container
 * or its storage draws this instead, so Icons.Default.Storage has no caller.
 */
object DsIcons {
    val Logo: ImageVector by lazy {
        ImageVector.Builder(name = "Droidspaces", defaultWidth = 24.dp, defaultHeight = 24.dp, viewportWidth = 48f, viewportHeight = 48f).apply {
            // The paths are ic_launcher_foreground's. The bars span 28.7 of the 48
            // viewport; 1.4 brings them to the 40 units that match the 20/24 glyph box
            // Material icons use, centred on the mark's own centre.
            group(scaleX = 1.4f, scaleY = 1.4f, pivotX = 24f, pivotY = 24.2f) {
                addPath(pathData = addPathNodes("M36.85 30.16H11.15c-0.82 0 -1.48 0.68 -1.48 1.52v3.05c0 0.84 0.66 1.52 1.48 1.52h25.69c0.82 0 1.48 -0.68 1.48 -1.52v-3.05c0 -0.84 -0.66 -1.52 -1.48 -1.52ZM12.58 34.97c-0.95 0 -1.73 -0.79 -1.73 -1.77s0.77 -1.77 1.73 -1.77 1.73 0.79 1.73 1.77 -0.77 1.77 -1.73 1.77Z"), fill = SolidColor(Color.Black))
                addPath(pathData = addPathNodes("M36.85 21.00H11.15c-0.82 0 -1.48 0.68 -1.48 1.52v3.05c0 0.84 0.66 1.52 1.48 1.52h25.69c0.82 0 1.48 -0.68 1.48 -1.52v-3.05c0 -0.84 -0.66 -1.52 -1.48 -1.52ZM12.58 25.82c-0.95 0 -1.73 -0.79 -1.73 -1.77s0.77 -1.77 1.73 -1.77 1.73 0.79 1.73 1.77 -0.77 1.77 -1.73 1.77Z"), fill = SolidColor(Color.Black))
                addPath(pathData = addPathNodes("M36.84 11.74H11.15c-0.82 0 -1.48 0.68 -1.48 1.52v3.05c0 0.84 0.66 1.52 1.48 1.52h25.69c0.82 0 1.48 -0.68 1.48 -1.52v-3.05c0 -0.84 -0.66 -1.52 -1.48 -1.52ZM12.58 16.57c-0.95 0 -1.73 -0.79 -1.73 -1.77s0.77 -1.77 1.73 -1.77 1.73 0.79 1.73 1.77 -0.77 1.77 -1.73 1.77Z"), fill = SolidColor(Color.Black))
            }
        }.build()
    }
}
