package com.droidspaces.app.ui.screen
import androidx.compose.ui.graphics.Color

import androidx.compose.animation.core.*
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.droidspaces.app.R
import com.droidspaces.app.util.AnimationUtils
import kotlinx.coroutines.delay

private data class ShowcaseCard(val icon: ImageVector, val titleRes: Int, val descRes: Int)

@Composable
fun WelcomeScreen(onNavigateToRootCheck: () -> Unit) {
    val context = LocalContext.current

    var iconVisible by remember { mutableStateOf(false) }
    var titleVisible by remember { mutableStateOf(false) }
    var cardsVisible by remember { mutableStateOf(false) }

    LaunchedEffect(Unit) {
        delay(60); iconVisible = true
        delay(120); titleVisible = true
        delay(150); cardsVisible = true
    }

    val iconAlpha by animateFloatAsState(if (iconVisible) 1f else 0f, AnimationUtils.fadeInSpec(), label = "icon")
    val titleAlpha by animateFloatAsState(if (titleVisible) 1f else 0f, AnimationUtils.fadeInSpec(), label = "title")
    val cardsAlpha by animateFloatAsState(if (cardsVisible) 1f else 0f, AnimationUtils.fadeInSpec(), label = "cards")

    val cards = listOf(
        ShowcaseCard(Icons.Default.Terminal, R.string.feat_containers_title, R.string.feat_containers_desc),
        ShowcaseCard(Icons.Default.Speed, R.string.feat_overhead_title, R.string.feat_overhead_desc),
        ShowcaseCard(Icons.Default.Lock, R.string.feat_unkillable_title, R.string.feat_unkillable_desc),
        ShowcaseCard(Icons.Default.Settings, R.string.feat_init_title, R.string.feat_init_desc),
        ShowcaseCard(Icons.Default.Shield, R.string.feat_isolation_title, R.string.feat_isolation_desc),
        ShowcaseCard(Icons.Default.Usb, R.string.feat_hardware_title, R.string.feat_hardware_desc),
        ShowcaseCard(Icons.Default.VpnKey, R.string.feat_privileged_title, R.string.feat_privileged_desc),
        ShowcaseCard(Icons.Default.PowerSettingsNew, R.string.feat_autoboot_title, R.string.feat_autoboot_desc),
    )

    val btnShape = RoundedCornerShape(20.dp)

    Scaffold(
        containerColor = Color.Transparent,
        bottomBar = {
            Surface(
                modifier = Modifier.fillMaxWidth(),
                color = MaterialTheme.colorScheme.surfaceContainer,
                tonalElevation = 0.dp
            ) {
                Column(modifier = Modifier.fillMaxWidth()) {
                    HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.4f), thickness = 1.dp)
                    Surface(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(20.dp)
                            .navigationBarsPadding()
                            .clip(btnShape)
                            .clickable(onClick = onNavigateToRootCheck),
                        shape = btnShape,
                        color = MaterialTheme.colorScheme.primary,
                        tonalElevation = 0.dp
                    ) {
                        Box(modifier = Modifier.padding(vertical = 16.dp), contentAlignment = Alignment.Center) {
                            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                Icon(Icons.Default.RocketLaunch, contentDescription = null, modifier = Modifier.size(18.dp), tint = MaterialTheme.colorScheme.onPrimary)
                                Text(
                                    text = context.getString(R.string.get_started),
                                    style = MaterialTheme.typography.labelLarge,
                                    fontWeight = FontWeight.SemiBold,
                                    fontSize = 16.sp,
                                    color = MaterialTheme.colorScheme.onPrimary
                                )
                            }
                        }
                    }
                }
            }
        }
    ) { innerPadding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
                .padding(horizontal = 24.dp)
                .verticalScroll(rememberScrollState()),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Spacer(modifier = Modifier.height(44.dp))

            // Hero: Tux (Static as requested)
            Box(
                modifier = Modifier
                    .alpha(iconAlpha)
                    .size(160.dp),
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    painter = painterResource(id = R.drawable.ic_tux),
                    contentDescription = null,
                    modifier = Modifier.size(130.dp),
                    tint = MaterialTheme.colorScheme.primary
                )
            }

            Spacer(modifier = Modifier.height(24.dp))

            // Hero text
            Column(modifier = Modifier.alpha(titleAlpha), horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(
                    text = context.getString(R.string.app_name),
                    style = MaterialTheme.typography.displaySmall,
                    fontWeight = FontWeight.ExtraBold,
                    textAlign = TextAlign.Center
                )
                Text(
                    text = context.getString(R.string.welcome_tagline),
                    style = MaterialTheme.typography.bodyLarge,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    textAlign = TextAlign.Center
                )
            }

            Spacer(modifier = Modifier.height(28.dp))

            // Feature cards
            Column(
                modifier = Modifier.fillMaxWidth().alpha(cardsAlpha),
                verticalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                cards.forEach { card ->
                    Surface(
                        modifier = Modifier.fillMaxWidth(),
                        shape = RoundedCornerShape(20.dp),
                        color = MaterialTheme.colorScheme.surfaceContainer,
                        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.4f)),
                        tonalElevation = 0.dp
                    ) {
                        Row(
                            modifier = Modifier.padding(16.dp),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(14.dp)
                        ) {
                            Surface(
                                shape = RoundedCornerShape(12.dp),
                                color = MaterialTheme.colorScheme.primary.copy(alpha = 0.1f),
                                tonalElevation = 0.dp
                            ) {
                                Icon(
                                    imageVector = card.icon,
                                    contentDescription = null,
                                    modifier = Modifier.padding(10.dp).size(20.dp),
                                    tint = MaterialTheme.colorScheme.primary
                                )
                            }
                            Column(verticalArrangement = Arrangement.spacedBy(3.dp)) {
                                Text(
                                    text = context.getString(card.titleRes),
                                    style = MaterialTheme.typography.titleSmall,
                                    fontWeight = FontWeight.Bold
                                )
                                Text(
                                    text = context.getString(card.descRes),
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }
                        }
                    }
                }
            }

            Spacer(modifier = Modifier.height(28.dp))
        }
    }
}
