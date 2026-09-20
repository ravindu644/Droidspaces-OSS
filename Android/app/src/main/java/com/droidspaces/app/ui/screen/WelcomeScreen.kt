package com.droidspaces.app.ui.screen

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.RocketLaunch
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.Shield
import androidx.compose.material.icons.filled.Speed
import androidx.compose.material.icons.filled.Terminal
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.droidspaces.app.R
import com.droidspaces.app.ui.component.PrimaryActionBottomBar
import com.droidspaces.app.util.AnimationUtils
import kotlinx.coroutines.delay

private data class ShowcaseCard(val icon: ImageVector, val titleRes: Int, val descRes: Int)

@Composable
fun WelcomeScreen(onNavigateToRootCheck: () -> Unit) {
    val context = LocalContext.current
    val configuration = LocalConfiguration.current
    val isCompact = configuration.screenWidthDp < 360 || configuration.screenHeightDp < 520
    val isNarrow = configuration.screenWidthDp < 360

    var iconVisible by remember { mutableStateOf(false) }
    var titleVisible by remember { mutableStateOf(false) }
    var cardsVisible by remember { mutableStateOf(false) }

    LaunchedEffect(Unit) {
        delay(60); iconVisible = true
        delay(120); titleVisible = true
        delay(150); cardsVisible = true
    }

    val iconAlpha by animateFloatAsState(if (iconVisible) 1f else 0f, AnimationUtils.fadeInSpec(), label = "icon")
    val iconOffset by animateFloatAsState(if (iconVisible) 0f else 16f, AnimationUtils.mediumSpec(), label = "icon_offset")

    val titleAlpha by animateFloatAsState(if (titleVisible) 1f else 0f, AnimationUtils.fadeInSpec(), label = "title")
    val titleOffset by animateFloatAsState(if (titleVisible) 0f else 14f, AnimationUtils.mediumSpec(), label = "title_offset")

    val cardsAlpha by animateFloatAsState(if (cardsVisible) 1f else 0f, AnimationUtils.fadeInSpec(), label = "cards")
    val cardsOffset by animateFloatAsState(if (cardsVisible) 0f else 20f, AnimationUtils.mediumSpec(), label = "cards_offset")

    val cards = listOf(
        ShowcaseCard(Icons.Default.Terminal, R.string.feat_containers_title, R.string.feat_containers_desc),
        ShowcaseCard(Icons.Default.Speed, R.string.feat_overhead_title, R.string.feat_overhead_desc),
        ShowcaseCard(Icons.Default.Shield, R.string.feat_isolation_title, R.string.feat_isolation_desc),
        ShowcaseCard(Icons.Default.Settings, R.string.feat_init_title, R.string.feat_init_desc),
    )

    Scaffold(
        containerColor = Color.Transparent,
        bottomBar = {
            PrimaryActionBottomBar(
                label = context.getString(R.string.get_started),
                icon = Icons.Default.RocketLaunch,
                onClick = onNavigateToRootCheck,
                dividerAlpha = 0.4f,
                horizontalPadding = if (isCompact) 16.dp else 20.dp
            )
        }
    ) { innerPadding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
                .padding(horizontal = if (isCompact) 16.dp else 24.dp)
                .verticalScroll(rememberScrollState()),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Spacer(modifier = Modifier.height(if (isCompact) 12.dp else 24.dp))

            // Hero emblem: dual-surface engineered seal with Droidspaces server-chassis logo
            val emblemSize = if (isCompact) 64.dp else 80.dp
            val innerWellSize = if (isCompact) 48.dp else 60.dp
            val logoSize = if (isCompact) 36.dp else 48.dp

            Surface(
                modifier = Modifier
                    .graphicsLayer {
                        alpha = iconAlpha
                        translationY = iconOffset
                    }
                    .size(emblemSize),
                shape = RoundedCornerShape(if (isCompact) 16.dp else 24.dp),
                color = MaterialTheme.colorScheme.surfaceContainerHigh,
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f)),
                tonalElevation = 0.dp
            ) {
                Box(contentAlignment = Alignment.Center) {
                    Surface(
                        modifier = Modifier.size(innerWellSize),
                        shape = RoundedCornerShape(if (isCompact) 12.dp else 16.dp),
                        color = MaterialTheme.colorScheme.primary.copy(alpha = 0.08f),
                        border = BorderStroke(1.dp, MaterialTheme.colorScheme.primary.copy(alpha = 0.2f)),
                        tonalElevation = 0.dp
                    ) {
                        Box(contentAlignment = Alignment.Center) {
                            Icon(
                                painter = painterResource(id = R.drawable.ic_launcher_foreground),
                                contentDescription = null,
                                modifier = Modifier.size(logoSize),
                                tint = MaterialTheme.colorScheme.primary
                            )
                        }
                    }
                }
            }

            Spacer(modifier = Modifier.height(if (isCompact) 12.dp else 16.dp))

            // Hero text: punchy display wordmark and value proposition
            Column(
                modifier = Modifier.graphicsLayer {
                    alpha = titleAlpha
                    translationY = titleOffset
                },
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                Text(
                    text = context.getString(R.string.app_name),
                    style = if (isCompact) MaterialTheme.typography.headlineMedium else MaterialTheme.typography.headlineLarge,
                    fontWeight = FontWeight.Black,
                    letterSpacing = (-0.5).sp,
                    textAlign = TextAlign.Center
                )
                Text(
                    text = context.getString(R.string.welcome_tagline),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.75f),
                    textAlign = TextAlign.Center
                )
            }

            Spacer(modifier = Modifier.height(if (isCompact) 16.dp else 24.dp))

            // Feature showcase: dynamic bento grid (1 lead banner + 2 split tiles + 1 wide banner)
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .widthIn(max = 560.dp)
                    .graphicsLayer {
                        alpha = cardsAlpha
                        translationY = cardsOffset
                    },
                verticalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                // Bento Item 1: Marquee Lead Card
                BentoTileHorizontal(
                    card = cards[0],
                    isLead = true,
                    isCompact = isCompact
                )

                // Bento Item 2 & 3: Dual Pillar Split (side-by-side on phones, stacked on watches)
                if (isNarrow) {
                    BentoTileVertical(
                        card = cards[1],
                        isCompact = true
                    )
                    BentoTileVertical(
                        card = cards[2],
                        isCompact = true
                    )
                } else {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(IntrinsicSize.Min),
                        horizontalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        BentoTileVertical(
                            card = cards[1],
                            isCompact = false,
                            modifier = Modifier
                                .weight(1f)
                                .fillMaxHeight()
                        )
                        BentoTileVertical(
                            card = cards[2],
                            isCompact = false,
                            modifier = Modifier
                                .weight(1f)
                                .fillMaxHeight()
                        )
                    }
                }

                // Bento Item 4: Wide Integration Banner
                BentoTileHorizontal(
                    card = cards[3],
                    isLead = false,
                    isCompact = isCompact
                )
            }

            Spacer(modifier = Modifier.height(if (isCompact) 16.dp else 24.dp))
        }
    }
}

@Composable
private fun BentoTileHorizontal(
    card: ShowcaseCard,
    isLead: Boolean,
    isCompact: Boolean,
    modifier: Modifier = Modifier
) {
    val context = LocalContext.current
    val accent = if (isLead) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.outlineVariant
    val bg = if (isLead) MaterialTheme.colorScheme.surfaceContainerHigh else MaterialTheme.colorScheme.surfaceContainer
    val borderAlpha = if (isLead) 0.4f else 0.35f

    Surface(
        modifier = modifier.fillMaxWidth(),
        shape = RoundedCornerShape(20.dp),
        color = bg,
        border = BorderStroke(1.dp, accent.copy(alpha = borderAlpha)),
        tonalElevation = 0.dp
    ) {
        Row(
            modifier = Modifier.padding(if (isCompact) 12.dp else 16.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Surface(
                shape = RoundedCornerShape(12.dp),
                color = MaterialTheme.colorScheme.primary.copy(alpha = if (isLead) 0.12f else 0.08f),
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.primary.copy(alpha = if (isLead) 0.25f else 0.15f)),
                tonalElevation = 0.dp
            ) {
                Icon(
                    imageVector = card.icon,
                    contentDescription = null,
                    modifier = Modifier
                        .padding(8.dp)
                        .size(if (isCompact) 18.dp else 20.dp),
                    tint = MaterialTheme.colorScheme.primary
                )
            }
            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                Text(
                    text = context.getString(card.titleRes),
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.SemiBold,
                    color = MaterialTheme.colorScheme.onSurface
                )
                Text(
                    text = context.getString(card.descRes),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
                )
            }
        }
    }
}

@Composable
private fun BentoTileVertical(
    card: ShowcaseCard,
    isCompact: Boolean,
    modifier: Modifier = Modifier
) {
    val context = LocalContext.current
    Surface(
        modifier = modifier.fillMaxWidth(),
        shape = RoundedCornerShape(20.dp),
        color = MaterialTheme.colorScheme.surfaceContainer,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f)),
        tonalElevation = 0.dp
    ) {
        Column(
            modifier = Modifier.padding(if (isCompact) 12.dp else 16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Surface(
                shape = RoundedCornerShape(12.dp),
                color = MaterialTheme.colorScheme.primary.copy(alpha = 0.08f),
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.primary.copy(alpha = 0.15f)),
                tonalElevation = 0.dp
            ) {
                Icon(
                    imageVector = card.icon,
                    contentDescription = null,
                    modifier = Modifier
                        .padding(8.dp)
                        .size(if (isCompact) 18.dp else 20.dp),
                    tint = MaterialTheme.colorScheme.primary
                )
            }
            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text(
                    text = context.getString(card.titleRes),
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.SemiBold,
                    color = MaterialTheme.colorScheme.onSurface
                )
                Text(
                    text = context.getString(card.descRes),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
                )
            }
        }
    }
}
