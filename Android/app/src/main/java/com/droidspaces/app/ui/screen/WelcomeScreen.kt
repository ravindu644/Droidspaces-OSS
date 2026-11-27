package com.droidspaces.app.ui.screen

import androidx.activity.compose.BackHandler
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.core.animateDpAsState
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowForward
import androidx.compose.material.icons.filled.Lock
import androidx.compose.material.icons.filled.PowerSettingsNew
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.Shield
import androidx.compose.material.icons.filled.Speed
import androidx.compose.material.icons.filled.Terminal
import androidx.compose.material.icons.filled.Usb
import androidx.compose.material.icons.filled.VpnKey
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialShapes
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.graphics.shapes.Morph
import com.droidspaces.app.R
import com.droidspaces.app.ui.component.MorphShape
import com.droidspaces.app.ui.component.SetupActionBar
import com.droidspaces.app.ui.component.SetupBody
import com.droidspaces.app.ui.component.SetupFrame
import com.droidspaces.app.ui.component.SetupFrameHeight
import com.droidspaces.app.ui.component.SetupGlyphSize
import com.droidspaces.app.ui.component.SetupHeroSize
import com.droidspaces.app.ui.component.SetupPage

private class Feature(val icon: ImageVector, val titleRes: Int, val descRes: Int)

private val Features = listOf(
    Feature(Icons.Default.Terminal, R.string.feat_containers_title, R.string.feat_containers_desc),
    Feature(Icons.Default.Speed, R.string.feat_overhead_title, R.string.feat_overhead_desc),
    Feature(Icons.Default.Lock, R.string.feat_unkillable_title, R.string.feat_unkillable_desc),
    Feature(Icons.Default.Settings, R.string.feat_init_title, R.string.feat_init_desc),
    Feature(Icons.Default.Shield, R.string.feat_isolation_title, R.string.feat_isolation_desc),
    Feature(Icons.Default.Usb, R.string.feat_hardware_title, R.string.feat_hardware_desc),
    Feature(Icons.Default.VpnKey, R.string.feat_privileged_title, R.string.feat_privileged_desc),
    Feature(Icons.Default.PowerSettingsNew, R.string.feat_autoboot_title, R.string.feat_autoboot_desc),
)

/* Three presets cycled over the eight pages, so a swipe always morphs between two
 * different shapes. The welcome hero is a soft burst that settles into the first. */
private val PageShapes = listOf(MaterialShapes.Cookie9Sided, MaterialShapes.Clover4Leaf, MaterialShapes.Sunny)


/**
 * Welcome and the feature pager are one screen on purpose. The hero stays in the same
 * slot while Get started morphs the soft burst into the first cookie and fades the logo
 * into the first feature glyph, which a shared element across two destinations could not
 * guarantee without the experimental transition scope around the whole NavHost.
 */
@Composable
fun WelcomeScreen(onNavigateToRootCheck: () -> Unit) {
    var started by rememberSaveable { mutableStateOf(false) }
    BackHandler(enabled = started) { started = false }

    val motion = MaterialTheme.motionScheme
    val pagerState = rememberPagerState { Features.size }
    val page = pagerState.currentPage

    // The spatial spring overshoots, and Morph only means anything in 0..1.
    val enter by animateFloatAsState(
        targetValue = if (started) 1f else 0f,
        animationSpec = motion.defaultSpatialSpec(),
        label = "hero_enter"
    )
    val entered = enter.coerceIn(0f, 1f)
    // Back keeps the page, so the hero morphs to the soft burst from whatever shape the
    // user left on, and returns to it.
    val pageShape = PageShapes[page % PageShapes.size]
    val enterMorph = remember(pageShape) { Morph(MaterialShapes.SoftBurst, pageShape) }
    // The pair is always (lower page, higher page) and progress is the position between
    // them. currentPage flips at half a page, and both sides of the flip read the same
    // pair at the same progress, so the shape never jumps.
    val offset = pagerState.currentPageOffsetFraction
    val lower = if (offset >= 0f) page else (page - 1).coerceAtLeast(0)
    val higher = (lower + 1).coerceAtMost(Features.lastIndex)
    val pageMorph = remember(lower, higher) {
        Morph(PageShapes[lower % PageShapes.size], PageShapes[higher % PageShapes.size])
    }
    val progress = when {
        offset >= 0f -> offset
        page == 0 -> 0f
        else -> 1f + offset
    }
    val heroShape = if (entered < 1f) MorphShape(enterMorph, entered) else MorphShape(pageMorph, progress)

    Scaffold(
        containerColor = Color.Transparent,
        bottomBar = {
            SetupActionBar(
                label = stringResource(if (started) R.string.next else R.string.get_started),
                icon = Icons.AutoMirrored.Filled.ArrowForward,
                onClick = { if (started) onNavigateToRootCheck() else started = true }
            )
        }
    ) { innerPadding ->
        SetupPage(
            innerPadding = innerPadding,
            above = {
                androidx.compose.animation.AnimatedVisibility(
                    visible = started,
                    enter = fadeIn(motion.defaultEffectsSpec()),
                    exit = fadeOut(motion.defaultEffectsSpec())
                ) {
                    Text(
                        text = stringResource(R.string.page_counter, page + 1, Features.size),
                        style = MaterialTheme.typography.labelLarge,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            },
            below = {
                androidx.compose.animation.AnimatedVisibility(
                    visible = started,
                    enter = fadeIn(motion.defaultEffectsSpec()),
                    exit = fadeOut(motion.defaultEffectsSpec())
                ) {
                    Row(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalAlignment = Alignment.CenterVertically) {
                        Features.indices.forEach { i ->
                            val selected = i == page
                            val width by animateDpAsState(
                                targetValue = if (selected) 24.dp else 8.dp,
                                animationSpec = motion.defaultSpatialSpec(),
                                label = "dot"
                            )
                            Box(
                                modifier = Modifier
                                    .size(width = width, height = 8.dp)
                                    .clip(CircleShape)
                                    .background(
                                        if (selected) MaterialTheme.colorScheme.primary
                                        else MaterialTheme.colorScheme.outlineVariant
                                    )
                            )
                        }
                    }
                }
            }
        ) {
            // The shape sits behind the frame and never moves. The pages slide over it.
            Column(modifier = Modifier.height(SetupFrameHeight)) {
                Box(
                    modifier = Modifier
                        .size(SetupHeroSize)
                        .clip(heroShape)
                        .background(MaterialTheme.colorScheme.primaryContainer)
                )
            }
            AnimatedContent(
                targetState = started,
                transitionSpec = {
                    fadeIn(motion.defaultEffectsSpec()).togetherWith(fadeOut(motion.defaultEffectsSpec()))
                },
                contentAlignment = Alignment.Center,
                label = "welcome_content"
            ) { pager ->
                if (pager) {
                    HorizontalPager(state = pagerState, modifier = Modifier.fillMaxWidth().height(SetupFrameHeight)) { i ->
                        val feature = Features[i]
                        SetupFrame(
                            title = stringResource(feature.titleRes),
                            hero = {
                                Icon(
                                    imageVector = feature.icon,
                                    contentDescription = null,
                                    modifier = Modifier.size(SetupGlyphSize),
                                    tint = MaterialTheme.colorScheme.onPrimaryContainer
                                )
                            }
                        ) { SetupBody(stringResource(feature.descRes)) }
                    }
                } else {
                    SetupFrame(
                        title = stringResource(R.string.welcome_headline),
                        titleStyle = MaterialTheme.typography.displaySmall,
                        hero = {
                            // The adaptive icon keeps its bars at 40% of the drawable, so
                            // drawing it at the hero size lands them at the glyph size.
                            Icon(
                                painter = painterResource(R.drawable.ic_launcher_foreground),
                                contentDescription = null,
                                modifier = Modifier.size(SetupHeroSize),
                                tint = MaterialTheme.colorScheme.onPrimaryContainer
                            )
                        }
                    ) { SetupBody(stringResource(R.string.welcome_body)) }
                }
            }
        }
    }
}

