package com.droidspaces.app.ui.component

import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialShapes
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.toShape
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.droidspaces.app.ui.util.LoadingIndicator
import com.droidspaces.app.ui.util.LoadingSize

/* One geometry for every setup screen, so the hero, the title and the copy land on the
 * same rows from the welcome page through the installer. The text block is a fixed box
 * so the frame has one height whatever the copy does. */
val SetupHeroSize = 240.dp
val SetupGlyphSize = 96.dp
private val TextBlockHeight = 200.dp
val SetupFrameHeight = SetupHeroSize + 32.dp + TextBlockHeight

/**
 * The skeleton of a setup screen inside its Scaffold: a 40dp row [above] the frame slot
 * (the page counter on the welcome pager, empty elsewhere), the slot itself, which centres
 * its [content] when it fits and scrolls when it does not, and a 44dp row [below] (the
 * page dots). Every screen keeps both rows so the frame and the button never move between
 * screens.
 */
@Composable
fun SetupPage(
    innerPadding: PaddingValues,
    above: @Composable BoxScope.() -> Unit = {},
    below: @Composable BoxScope.() -> Unit = {},
    content: @Composable BoxScope.() -> Unit
) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(innerPadding)
            .padding(horizontal = 24.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Box(modifier = Modifier.height(40.dp), contentAlignment = Alignment.Center, content = above)
        Spacer(modifier = Modifier.height(16.dp))
        BoxWithConstraints(modifier = Modifier.fillMaxWidth().weight(1f)) {
            val slotHeight = maxHeight
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .heightIn(min = slotHeight)
                    .verticalScroll(rememberScrollState()),
                contentAlignment = Alignment.Center,
                content = content
            )
        }
        Box(modifier = Modifier.fillMaxWidth().height(44.dp), contentAlignment = Alignment.Center, content = below)
    }
}

/**
 * The frame inside the slot: [hero] in the 240dp hero box, then the title and [body] in a
 * fixed text block that scrolls if the copy ever outgrows it.
 */
@Composable
fun SetupFrame(
    title: String,
    modifier: Modifier = Modifier,
    titleStyle: TextStyle = MaterialTheme.typography.headlineMedium,
    hero: @Composable BoxScope.() -> Unit,
    body: @Composable ColumnScope.() -> Unit
) {
    Column(
        modifier = modifier.fillMaxWidth().height(SetupFrameHeight),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Box(modifier = Modifier.size(SetupHeroSize), contentAlignment = Alignment.Center, content = hero)
        Spacer(modifier = Modifier.height(32.dp))
        // Title and copy fade together on the same spec as the hero, so a state change
        // lands as one beat on every setup screen.
        val motion = MaterialTheme.motionScheme
        AnimatedContent(
            targetState = title,
            transitionSpec = {
                fadeIn(motion.fastEffectsSpec()).togetherWith(fadeOut(motion.fastEffectsSpec()))
            },
            contentAlignment = Alignment.TopCenter,
            label = "setup_copy"
        ) { current ->
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(TextBlockHeight)
                    .verticalScroll(rememberScrollState()),
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                Text(
                    text = current,
                    style = titleStyle,
                    fontWeight = FontWeight.Bold,
                    textAlign = TextAlign.Center
                )
                Spacer(modifier = Modifier.height(12.dp))
                body()
            }
        }
    }
}

/**
 * The hero of a screen that does work: a twelve-sided cookie tinted [container] holding
 * the loader while [loading], otherwise [glyph]. The container stays put through every
 * state and only its colour and its content change, so a result reads as the same thing
 * settling rather than a swap. State lives in the colour: a warning is amber, a failure
 * is red, success and idle are the primary container.
 */
@Composable
fun SetupHero(
    loading: Boolean,
    glyph: ImageVector,
    container: Color,
    onContainer: Color,
    modifier: Modifier = Modifier
) {
    val motion = MaterialTheme.motionScheme
    val tint by animateColorAsState(container, motion.fastEffectsSpec(), label = "hero_tint")
    Box(
        modifier = modifier
            .size(SetupHeroSize)
            .clip(MaterialShapes.Cookie12Sided.toShape())
            .background(tint),
        contentAlignment = Alignment.Center
    ) {
        // A plain fade in a box of one size. Letting the switch animate between the
        // loader's box and the glyph's box slides the glyph into place from the top.
        AnimatedContent(
            targetState = if (loading) null else glyph,
            transitionSpec = {
                fadeIn(motion.fastEffectsSpec()).togetherWith(fadeOut(motion.fastEffectsSpec()))
            },
            contentAlignment = Alignment.Center,
            label = "setup_hero"
        ) { icon ->
            Box(modifier = Modifier.size(SetupHeroSize), contentAlignment = Alignment.Center) {
                if (icon == null) {
                    LoadingIndicator(size = LoadingSize.Hero, color = onContainer)
                } else {
                    Icon(
                        imageVector = icon,
                        contentDescription = null,
                        modifier = Modifier.size(SetupGlyphSize),
                        tint = onContainer
                    )
                }
            }
        }
    }
}

/**
 * The setup flow's primary button. DESIGN.md's 20dp bar button with a divider is for
 * screens with content scrolling under it; here the button sits alone on the background
 * under a hero, so it is a full pill with no bar surface, the Expressive button shape the
 * welcome pages are built around. Disabled keeps the enabled colours: a setup button is
 * only disabled for the beat a check or install is in flight, and greying it reads as a
 * flicker. This is a decided deviation, listed in DESIGN.md.
 */
@Composable
fun SetupActionBar(
    label: String,
    icon: ImageVector,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true
) {
    PrimaryActionBottomBar(
        label = label,
        icon = icon,
        onClick = onClick,
        modifier = modifier,
        enabled = enabled,
        disabledContainerColor = MaterialTheme.colorScheme.primary,
        disabledContentColor = MaterialTheme.colorScheme.onPrimary,
        barColor = Color.Transparent,
        dividerAlpha = 0f,
        shape = CircleShape
    )
}

/** One line of copy under a setup title. */
@Composable
fun SetupBody(text: String, color: Color = MaterialTheme.colorScheme.onSurfaceVariant) {
    Text(
        text = text,
        style = MaterialTheme.typography.bodyLarge,
        color = color,
        textAlign = TextAlign.Center,
        modifier = Modifier.fillMaxWidth()
    )
}
