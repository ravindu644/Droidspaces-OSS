package com.droidspaces.app.ui.component

import androidx.compose.material3.toPath
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.geometry.center
import androidx.compose.ui.graphics.Matrix
import androidx.compose.ui.graphics.Outline
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.LayoutDirection
import androidx.graphics.shapes.Morph

/**
 * A [Shape] part way between two [androidx.compose.material3.MaterialShapes] presets.
 * [progress] 0 is [Morph.start], 1 is [Morph.end].
 *
 * `Shape.createOutline` is not composable, so this uses material3's plain `Morph.toPath`
 * rather than `RoundedPolygon.toShape()`. The scale and centring copy what `toShape` does
 * internally: the presets are normalised to a unit box, so they stretch to the layout size
 * and are then re-centred because a morph's bounds drift as it animates.
 */
data class MorphShape(private val morph: Morph, private val progress: Float) : Shape {
    override fun createOutline(size: Size, layoutDirection: LayoutDirection, density: Density): Outline {
        val path = morph.toPath(progress)
        val matrix = Matrix()
        matrix.scale(size.width, size.height)
        path.transform(matrix)
        path.translate(size.center - path.getBounds().center)
        return Outline.Generic(path)
    }
}
