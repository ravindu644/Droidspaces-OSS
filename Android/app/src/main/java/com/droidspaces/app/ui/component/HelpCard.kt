package com.droidspaces.app.ui.component

import android.content.Intent
import android.net.Uri
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ChevronRight
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.graphics.Color
import com.droidspaces.app.R

@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
fun HelpCard(
    modifier: Modifier = Modifier
) {
    val context = LocalContext.current
    val url = context.getString(R.string.help_card_url)

    // The one promotional row on Home: tertiaryContainer, the "new here?" accent
    Surface(
        onClick = { context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(url))) },
        modifier = modifier.fillMaxWidth(),
        shape = MaterialTheme.shapes.extraLarge,
        color = MaterialTheme.colorScheme.tertiaryContainer
    ) {
        val onContainer = MaterialTheme.colorScheme.onTertiaryContainer
        ListItem(
            headlineContent = {
                Text(context.getString(R.string.help_card_title), style = MaterialTheme.typography.bodyLargeEmphasized)
            },
            supportingContent = { Text(context.getString(R.string.help_card_message)) },
            trailingContent = { Icon(Icons.Default.ChevronRight, contentDescription = null) },
            colors = ListItemDefaults.colors(
                containerColor = Color.Transparent,
                headlineColor = onContainer,
                supportingColor = onContainer,
                trailingIconColor = onContainer
            )
        )
    }
}
