package com.droidspaces.app.ui.util

import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.AnimationEndReason
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.progressSemantics
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.geometry.center
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Matrix
import androidx.compose.ui.graphics.Outline
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.drawscope.Fill
import androidx.compose.ui.graphics.drawscope.rotate
import androidx.compose.ui.platform.InfiniteAnimationPolicy
import androidx.compose.ui.semantics.ProgressBarRangeInfo
import androidx.compose.ui.semantics.progressBarRangeInfo
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.compose.ui.util.fastForEach
import androidx.compose.ui.util.fastMap
import androidx.graphics.shapes.CornerRounding
import androidx.graphics.shapes.Cubic
import androidx.graphics.shapes.Morph
import androidx.graphics.shapes.RoundedPolygon
import androidx.graphics.shapes.TransformResult
import androidx.graphics.shapes.circle
import androidx.graphics.shapes.rectangle
import androidx.graphics.shapes.star
import kotlin.math.PI
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.sin
import kotlin.math.max
import kotlin.math.min
import kotlinx.coroutines.async
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

// ── COMPATIBILITY & DROIDSPACES CONVENIENCE WRAAPERS ───────────────────────

/**
 * Standardized loading indicator sizes.
 */
enum class LoadingSize(val size: Dp, val strokeWidth: Dp) {
    Small(16.dp, 2.dp),
    Medium(24.dp, 3.dp),
    Large(48.dp, 4.dp)
}

/**
 * Standardized loading indicator component (Convenience compatibility wrapper).
 */
@Composable
fun LoadingIndicator(
    size: LoadingSize,
    modifier: Modifier = Modifier,
    color: Color? = null
) {
    LoadingIndicator(
        modifier = modifier.size(size.size),
        color = color ?: MaterialTheme.colorScheme.primary
    )
}

/**
 * Full-screen loading indicator with optional message.
 */
@Composable
fun FullScreenLoading(
    message: String? = null,
    modifier: Modifier = Modifier
) {
    Box(
        modifier = modifier.fillMaxSize(),
        contentAlignment = Alignment.Center
    ) {
        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            LoadingIndicator(size = LoadingSize.Large)
            message?.let {
                Text(
                    text = it,
                    style = MaterialTheme.typography.bodyLarge,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    textAlign = TextAlign.Center
                )
            }
        }
    }
}

// ── OFFICIAL GOOGLE MATERIAL 3 EXPRESSIVE APIs ──────────────────────────────

/**
 * A Material Design loading indicator.
 *
 * This version of the loading indicator morphs between its [polygons] shapes by the value of its
 * [progress].
 *
 * @param progress the progress of this loading indicator, where 0.0 represents no progress and 1.0
 *   represents full progress. Values outside of this range are coerced into the range.
 * @param modifier the [Modifier] to be applied to this loading indicator
 * @param color the loading indicator's color
 * @param polygons a list of [RoundedPolygon]s for the sequence of shapes this loading indicator
 *   will morph between as it progresses from 0.0 to 1.0.
 */
@Composable
fun LoadingIndicator(
    progress: () -> Float,
    modifier: Modifier = Modifier,
    color: Color = LoadingIndicatorDefaults.indicatorColor,
    polygons: List<RoundedPolygon> = LoadingIndicatorDefaults.DeterminateIndicatorPolygons,
) {
    LoadingIndicatorImpl(
        progress = progress,
        modifier = modifier,
        containerColor = Color.Unspecified,
        indicatorColor = color,
        containerShape = LoadingIndicatorDefaults.containerShape,
        indicatorPolygons = polygons,
    )
}

/**
 * A Material Design loading indicator.
 *
 * This version of the loading indicator animates and morphs between various shapes as long as the
 * loading indicator is visible.
 *
 * @param modifier the [Modifier] to be applied to this loading indicator
 * @param color the loading indicator's color
 * @param polygons a list of [RoundedPolygon]s for the sequence of shapes this loading indicator
 *   will morph between.
 */
@Composable
fun LoadingIndicator(
    modifier: Modifier = Modifier,
    color: Color = LoadingIndicatorDefaults.indicatorColor,
    polygons: List<RoundedPolygon> = LoadingIndicatorDefaults.IndeterminateIndicatorPolygons,
) {
    LoadingIndicatorImpl(
        modifier = modifier,
        containerColor = Color.Unspecified,
        indicatorColor = color,
        containerShape = LoadingIndicatorDefaults.containerShape,
        indicatorPolygons = polygons,
    )
}

/**
 * A Material Design contained loading indicator.
 *
 * This version of the loading indicator morphs between its [polygons] shapes by the value of its
 * [progress]. The shapes in this variation are contained within a colored [containerShape].
 *
 * @param progress the progress of this loading indicator, where 0.0 represents no progress and 1.0
 *   represents full progress. Values outside of this range are coerced into the range.
 * @param modifier the [Modifier] to be applied to this loading indicator
 * @param containerColor the loading indicator's container color
 * @param indicatorColor the loading indicator's color
 * @param containerShape the loading indicator's container shape
 * @param polygons a list of [RoundedPolygon]s for the sequence of shapes this loading indicator
 *   will morph between as it progresses from 0.0 to 1.0.
 */
@Composable
fun ContainedLoadingIndicator(
    progress: () -> Float,
    modifier: Modifier = Modifier,
    containerColor: Color = LoadingIndicatorDefaults.containedContainerColor,
    indicatorColor: Color = LoadingIndicatorDefaults.containedIndicatorColor,
    containerShape: Shape = LoadingIndicatorDefaults.containerShape,
    polygons: List<RoundedPolygon> = LoadingIndicatorDefaults.DeterminateIndicatorPolygons,
) {
    LoadingIndicatorImpl(
        progress = progress,
        modifier = modifier,
        containerColor = containerColor,
        indicatorColor = indicatorColor,
        containerShape = containerShape,
        indicatorPolygons = polygons,
    )
}

/**
 * A Material Design contained loading indicator.
 *
 * This version of the loading indicator animates and morphs between various shapes as long as the
 * loading indicator is visible. The shapes in this variation are contained within a colored
 * [containerShape].
 *
 * @param modifier the [Modifier] to be applied to this loading indicator
 * @param containerColor the loading indicator's container color
 * @param indicatorColor the loading indicator's color
 * @param containerShape the loading indicator's container shape
 * @param polygons a list of [RoundedPolygon]s for the sequence of shapes this loading indicator
 *   will morph between.
 */
@Composable
fun ContainedLoadingIndicator(
    modifier: Modifier = Modifier,
    containerColor: Color = LoadingIndicatorDefaults.containedContainerColor,
    indicatorColor: Color = LoadingIndicatorDefaults.containedIndicatorColor,
    containerShape: Shape = LoadingIndicatorDefaults.containerShape,
    polygons: List<RoundedPolygon> = LoadingIndicatorDefaults.IndeterminateIndicatorPolygons,
) {
    LoadingIndicatorImpl(
        modifier = modifier,
        containerColor = containerColor,
        indicatorColor = indicatorColor,
        containerShape = containerShape,
        indicatorPolygons = polygons,
    )
}

// ── INTERNAL IMPLEMENTATION DETAILS ─────────────────────────────────────────

@Composable
private fun LoadingIndicatorImpl(
    progress: () -> Float,
    modifier: Modifier,
    containerColor: Color,
    indicatorColor: Color,
    containerShape: Shape,
    indicatorPolygons: List<RoundedPolygon>,
) {
    require(indicatorPolygons.size > 1) {
        "indicatorPolygons should have, at least, two RoundedPolygons"
    }
    val coercedProgress = { progress().coerceIn(0f, 1f) }
    val path = remember { Path() }
    val scaleMatrix = remember { Matrix() }
    val morphSequence = remember(indicatorPolygons) {
        morphSequence(polygons = indicatorPolygons, circularSequence = false)
    }
    val morphScaleFactor = remember(morphSequence) {
        calculateScaleFactor(indicatorPolygons) * LoadingIndicatorDefaults.ActiveIndicatorScale
    }
    Box(
        modifier = modifier
            .semantics(mergeDescendants = true) {
                progressBarRangeInfo = ProgressBarRangeInfo(
                    coercedProgress().takeUnless { it.isNaN() } ?: 0f,
                    0f..1f,
                )
            }
            .size(
                width = LoadingIndicatorDefaults.ContainerWidth,
                height = LoadingIndicatorDefaults.ContainerHeight,
            )
            .fillMaxSize()
            .clip(containerShape)
            .background(containerColor),
        contentAlignment = Alignment.Center,
    ) {
        Spacer(
            modifier = Modifier
                .aspectRatio(ratio = 1f, matchHeightConstraintsFirst = true)
                .drawWithContent {
                    val progressValue = coercedProgress()
                    val activeMorphIndex = (morphSequence.size * progressValue)
                        .toInt()
                        .coerceAtMost(morphSequence.size - 1)
                    val adjustedProgressValue = if (progressValue == 1f && activeMorphIndex == morphSequence.size - 1) {
                        1f
                    } else {
                        (progressValue * morphSequence.size) % 1f
                    }

                    val rotation = -progressValue * 180
                    rotate(rotation) {
                        drawPath(
                            path = processPath(
                                path = morphSequence[activeMorphIndex].toPath(
                                    progress = adjustedProgressValue,
                                    path = path,
                                    startAngle = 0,
                                ),
                                size = size,
                                scaleFactor = morphScaleFactor,
                                scaleMatrix = scaleMatrix,
                            ),
                            color = indicatorColor,
                            style = Fill,
                        )
                    }
                }
        )
    }
}

@Composable
private fun LoadingIndicatorImpl(
    modifier: Modifier,
    containerColor: Color,
    indicatorColor: Color,
    containerShape: Shape,
    indicatorPolygons: List<RoundedPolygon>,
) {
    require(indicatorPolygons.size > 1) {
        "indicatorPolygons should have, at least, two RoundedPolygons"
    }
    val morphSequence = remember(indicatorPolygons) {
        morphSequence(polygons = indicatorPolygons, circularSequence = true)
    }
    val shapesScaleFactor = remember(indicatorPolygons) {
        calculateScaleFactor(indicatorPolygons) * LoadingIndicatorDefaults.ActiveIndicatorScale
    }
    val morphProgress = remember { Animatable(0f) }
    var morphRotationTargetAngle by remember { mutableFloatStateOf(QuarterRotation) }
    val globalRotation = remember { Animatable(0f) }
    var currentMorphIndex by remember(indicatorPolygons) { mutableIntStateOf(0) }

    LaunchedEffect(indicatorPolygons) {
        val morphAnimationBlock = {
            launch {
                val morphAnimationSpec = spring<Float>(dampingRatio = 0.6f, stiffness = 200f, visibilityThreshold = 0.1f)
                while (true) {
                    val deferred = async {
                        val animationResult = morphProgress.animateTo(
                            targetValue = 1f,
                            animationSpec = morphAnimationSpec,
                        )
                        if (animationResult.endReason == AnimationEndReason.Finished) {
                            currentMorphIndex = (currentMorphIndex + 1) % morphSequence.size
                            morphProgress.snapTo(0f)
                            morphRotationTargetAngle = (morphRotationTargetAngle + QuarterRotation) % FullRotation
                        }
                    }
                    delay(MorphIntervalMillis)
                    deferred.await()
                }
            }
        }

        val rotationAnimationBlock = {
            launch {
                globalRotation.animateTo(
                    targetValue = FullRotation,
                    animationSpec = infiniteRepeatable(
                        tween(GlobalRotationDurationMillis, easing = LinearEasing),
                        repeatMode = RepeatMode.Restart,
                    ),
                )
            }
        }

        when (val policy = coroutineContext[InfiniteAnimationPolicy]) {
            null -> {
                morphAnimationBlock()
                rotationAnimationBlock()
            }
            else -> policy.onInfiniteOperation {
                morphAnimationBlock()
                rotationAnimationBlock()
            }
        }
    }

    val path = remember { Path() }
    val scaleMatrix = remember { Matrix() }
    Box(
        modifier = modifier
            .progressSemantics()
            .size(
                width = LoadingIndicatorDefaults.ContainerWidth,
                height = LoadingIndicatorDefaults.ContainerHeight,
            )
            .fillMaxSize()
            .clip(containerShape)
            .background(containerColor),
        contentAlignment = Alignment.Center,
    ) {
        Spacer(
            modifier = Modifier
                .aspectRatio(1f, matchHeightConstraintsFirst = true)
                .drawWithContent {
                    val progress = morphProgress.value
                    rotate(progress * 90 + morphRotationTargetAngle + globalRotation.value) {
                        drawPath(
                            path = processPath(
                                path = morphSequence[currentMorphIndex].toPath(
                                    progress = progress,
                                    path = path,
                                    startAngle = 0,
                                ),
                                size = size,
                                scaleFactor = shapesScaleFactor,
                                scaleMatrix = scaleMatrix,
                            ),
                            color = indicatorColor,
                            style = Fill,
                        )
                    }
                }
        )
    }
}

// ── LOADING INDICATOR DEFAULTS ──────────────────────────────────────────────
object LoadingIndicatorDefaults {
    val ContainerWidth: Dp = 48.dp
    val ContainerHeight: Dp = 48.dp
    val IndicatorSize: Dp = 40.dp

    val containerShape: Shape
        @Composable get() = RoundedCornerShape(16.dp)

    val indicatorColor: Color
        @Composable get() = MaterialTheme.colorScheme.primary

    val containedIndicatorColor: Color
        @Composable get() = MaterialTheme.colorScheme.primary

    val containedContainerColor: Color
        @Composable get() = MaterialTheme.colorScheme.surfaceContainerHigh

    val IndeterminateIndicatorPolygons: List<RoundedPolygon> = listOf(
        MaterialShapes.SoftBurst,
        MaterialShapes.Cookie9Sided,
        MaterialShapes.Pentagon,
        MaterialShapes.Pill,
        MaterialShapes.Sunny,
        MaterialShapes.Cookie4Sided,
        MaterialShapes.Oval,
    )

    val DeterminateIndicatorPolygons: List<RoundedPolygon> = listOf(
        MaterialShapes.Circle.transformed(Matrix().apply { rotateZ(360f / 20) }),
        MaterialShapes.SoftBurst,
    )

    internal val ActiveIndicatorScale =
        IndicatorSize.value / min(ContainerWidth.value, ContainerHeight.value)
}

// ── SHAPE UTIL EXTENSIONS (TRANSFORM & PATH CONVERSION) ────────────────────
internal fun RoundedPolygon.transformed(matrix: Matrix): RoundedPolygon = transformed { x, y ->
    val transformedPoint = matrix.map(Offset(x, y))
    TransformResult(transformedPoint.x, transformedPoint.y)
}

internal fun Morph.toPath(
    progress: Float,
    path: Path = Path(),
    startAngle: Int = 270,
    repeatPath: Boolean = false,
    closePath: Boolean = true,
    rotationPivotX: Float = 0f,
    rotationPivotY: Float = 0f,
): Path {
    pathFromCubics(
        path = path,
        startAngle = startAngle,
        repeatPath = repeatPath,
        closePath = closePath,
        cubics = asCubics(progress),
        rotationPivotX = rotationPivotX,
        rotationPivotY = rotationPivotY,
    )
    return path
}

private fun pathFromCubics(
    path: Path,
    startAngle: Int,
    repeatPath: Boolean,
    closePath: Boolean,
    cubics: List<Cubic>,
    rotationPivotX: Float,
    rotationPivotY: Float,
) {
    var first = true
    var firstCubic: Cubic? = null
    path.rewind()
    cubics.fastForEach {
        if (first) {
            path.moveTo(it.anchor0X, it.anchor0Y)
            if (startAngle != 0) {
                firstCubic = it
            }
            first = false
        }
        path.cubicTo(
            it.control0X,
            it.control0Y,
            it.control1X,
            it.control1Y,
            it.anchor1X,
            it.anchor1Y,
        )
    }
    if (repeatPath) {
        var firstInRepeat = true
        cubics.fastForEach {
            if (firstInRepeat) {
                path.lineTo(it.anchor0X, it.anchor0Y)
                firstInRepeat = false
            }
            path.cubicTo(
                it.control0X,
                it.control0Y,
                it.control1X,
                it.control1Y,
                it.anchor1X,
                it.anchor1Y,
            )
        }
    }

    if (closePath) path.close()

    if (startAngle != 0 && firstCubic != null) {
        val angleToFirstCubic = radiansToDegrees(
            atan2(
                y = cubics[0].anchor0Y - rotationPivotY,
                x = cubics[0].anchor0X - rotationPivotX,
            )
        )
        path.transform(Matrix().apply { rotateZ(-angleToFirstCubic + startAngle) })
    }
}

private fun radiansToDegrees(radians: Float): Float {
    return (radians * 180.0 / PI).toFloat()
}

private fun morphSequence(polygons: List<RoundedPolygon>, circularSequence: Boolean): List<Morph> {
    return buildList {
        for (i in polygons.indices) {
            if (i + 1 < polygons.size) {
                add(Morph(polygons[i].normalized(), polygons[i + 1].normalized()))
            } else if (circularSequence) {
                add(Morph(polygons[i].normalized(), polygons[0].normalized()))
            }
        }
    }
}

private fun calculateScaleFactor(indicatorPolygons: List<RoundedPolygon>): Float {
    var scaleFactor = 1f
    val bounds = FloatArray(size = 4)
    val maxBounds = FloatArray(size = 4)
    indicatorPolygons.fastForEach { polygon ->
        polygon.calculateBounds(bounds)
        polygon.calculateMaxBounds(maxBounds)
        val scaleX = bounds.width() / maxBounds.width()
        val scaleY = bounds.height() / maxBounds.height()
        scaleFactor = min(scaleFactor, max(scaleX, scaleY))
    }
    return scaleFactor
}

private fun FloatArray.width(): Float = this[2] - this[0]
private fun FloatArray.height(): Float = this[3] - this[1]

private fun processPath(
    path: Path,
    size: Size,
    scaleFactor: Float,
    scaleMatrix: Matrix = Matrix(),
): Path {
    scaleMatrix.reset()
    scaleMatrix.apply { scale(x = size.width * scaleFactor, y = size.height * scaleFactor) }
    path.transform(scaleMatrix)
    path.translate(size.center - path.getBounds().center)
    return path
}

// ── PREDEFINED MATERIAL SHAPES ──────────────────────────────────────────────
object MaterialShapes {
    private val cornerRound15 = CornerRounding(radius = .15f)
    private val cornerRound20 = CornerRounding(radius = .2f)
    private val cornerRound30 = CornerRounding(radius = .3f)
    private val cornerRound50 = CornerRounding(radius = .5f)
    private val cornerRound100 = CornerRounding(radius = 1f)

    private val rotateNeg45 = Matrix().apply { rotateZ(-45f) }
    private val rotateNeg90 = Matrix().apply { rotateZ(-90f) }
    private val rotateNeg135 = Matrix().apply { rotateZ(-135f) }

    val Circle: RoundedPolygon = RoundedPolygon.circle(numVertices = 10).normalized()
    val Square: RoundedPolygon = RoundedPolygon.rectangle(width = 1f, height = 1f, rounding = cornerRound30).normalized()
    val Oval: RoundedPolygon = RoundedPolygon.circle().transformed(Matrix().apply { scale(1f, 0.64f) }).transformed(rotateNeg45).normalized()
    
    val Pill: RoundedPolygon = customPolygon(
        listOf(
            PointNRound(Offset(0.961f, 0.039f), CornerRounding(0.426f)),
            PointNRound(Offset(1.001f, 0.428f)),
            PointNRound(Offset(1.000f, 0.609f), CornerRounding(1.000f)),
        ),
        reps = 2,
        mirroring = true,
    ).normalized()

    val Pentagon: RoundedPolygon = customPolygon(
        listOf(
            PointNRound(Offset(0.500f, -0.009f), CornerRounding(0.172f)),
            PointNRound(Offset(1.030f, 0.365f), CornerRounding(0.164f)),
            PointNRound(Offset(0.828f, 0.970f), CornerRounding(0.169f)),
        ),
        reps = 1,
        mirroring = true,
    ).normalized()

    val Sunny: RoundedPolygon = RoundedPolygon.star(
        numVerticesPerRadius = 8,
        innerRadius = .8f,
        rounding = cornerRound15,
    ).normalized()

    val Cookie4Sided: RoundedPolygon = customPolygon(
        listOf(
            PointNRound(Offset(1.237f, 1.236f), CornerRounding(0.258f)),
            PointNRound(Offset(0.500f, 0.918f), CornerRounding(0.233f)),
        ),
        4,
    ).normalized()

    val Cookie9Sided: RoundedPolygon = RoundedPolygon.star(
        numVerticesPerRadius = 9,
        innerRadius = .8f,
        rounding = cornerRound50,
    ).transformed(rotateNeg90).normalized()

    val SoftBurst: RoundedPolygon = customPolygon(
        listOf(
            PointNRound(Offset(0.193f, 0.277f), CornerRounding(0.053f)),
            PointNRound(Offset(0.176f, 0.055f), CornerRounding(0.053f)),
        ),
        reps = 10,
    ).normalized()

    private data class PointNRound(
        val o: Offset,
        val r: CornerRounding = CornerRounding.Unrounded,
    )

    private fun doRepeat(
        points: List<PointNRound>,
        reps: Int,
        center: Offset,
        mirroring: Boolean,
    ) = if (mirroring) {
        buildList {
            val angles = points.fastMap { (it.o - center).angleDegrees() }
            val distances = points.fastMap { (it.o - center).getDistance() }
            val actualReps = reps * 2
            val sectionAngle = 360f / actualReps
            repeat(actualReps) {
                points.indices.forEach { index ->
                    val i = if (it % 2 == 0) index else points.lastIndex - index
                    if (i > 0 || it % 2 == 0) {
                        val a = (sectionAngle * it +
                                if (it % 2 == 0) angles[i]
                                else sectionAngle - angles[i] + 2 * angles[0])
                            .toRadians()
                        val finalPoint = Offset(cos(a), sin(a)) * distances[i] + center
                        add(PointNRound(finalPoint, points[i].r))
                    }
                }
            }
        }
    } else {
        points.size.let { np ->
            (0 until np * reps).map {
                val point = points[it % np].o.rotateDegrees((it / np) * 360f / reps, center)
                PointNRound(point, points[it % np].r)
            }
        }
    }

    private fun Offset.rotateDegrees(angle: Float, center: Offset = Offset.Zero) =
        (angle.toRadians()).let { a ->
            val off = this - center
            Offset(off.x * cos(a) - off.y * sin(a), off.x * sin(a) + off.y * cos(a)) + center
        }

    private fun Float.toRadians(): Float = this / 360f * 2 * PI.toFloat()

    private fun Offset.angleDegrees() = atan2(y, x) * 180f / PI.toFloat()

    private fun customPolygon(
        pnr: List<PointNRound>,
        reps: Int,
        center: Offset = Offset(0.5f, 0.5f),
        mirroring: Boolean = false,
    ): RoundedPolygon {
        val actualPoints = doRepeat(pnr, reps, center, mirroring)
        return RoundedPolygon(
            vertices = FloatArray(actualPoints.size * 2) { ix ->
                actualPoints[ix / 2].o.let { if (ix % 2 == 0) it.x else it.y }
            },
            perVertexRounding = buildList { for (p in actualPoints) add(p.r) },
            centerX = center.x,
            centerY = center.y,
        )
    }
}

private const val GlobalRotationDurationMillis = 4666
private const val MorphIntervalMillis = 650L

private const val FullRotation = 360f
private const val QuarterRotation = FullRotation / 4f
