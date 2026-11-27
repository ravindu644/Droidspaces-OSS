package com.droidspaces.app.ui.component

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.platform.LocalContext
import com.droidspaces.app.R

/**
 * Reusable empty state composable for consistent empty states across the app.
 * Centralizes the common pattern of icon + title + description, with an
 * optional retry button for states a second attempt can clear.
 */
@Composable
fun EmptyState(
    icon: ImageVector,
    title: String,
    description: String,
    modifier: Modifier = Modifier,
    onRetry: (() -> Unit)? = null
) {
    val context = LocalContext.current
    Column(
        modifier = modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(32.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        Icon(
            imageVector = icon,
            contentDescription = null,
            modifier = Modifier.size(64.dp),
            tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f)
        )
        Spacer(modifier = Modifier.height(16.dp))
        Text(
            text = title,
            style = MaterialTheme.typography.titleLarge,
            fontWeight = FontWeight.SemiBold,
            textAlign = TextAlign.Center
        )
        Spacer(modifier = Modifier.height(8.dp))
        Text(
            text = description,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f),
            textAlign = TextAlign.Center
        )
        if (onRetry != null) {
            Spacer(modifier = Modifier.height(16.dp))
            Surface(
                onClick = onRetry,
                modifier = Modifier.height(48.dp),
                shape = RoundedCornerShape(16.dp),
                color = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.4f),
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.primary.copy(alpha = 0.2f)),
                tonalElevation = 0.dp
            ) {
                Box(modifier = Modifier.padding(horizontal = 24.dp), contentAlignment = Alignment.Center) {
                    Text(
                        text = context.getString(R.string.repo_retry),
                        style = MaterialTheme.typography.labelLarge,
                        fontWeight = FontWeight.SemiBold,
                        color = MaterialTheme.colorScheme.primary
                    )
                }
            }
        }
    }
}

/**
 * Reusable error state composable for consistent error states across the app.
 */
@Composable
fun ErrorState(
    title: String = "",
    description: String = "",
    modifier: Modifier = Modifier,
    onRetry: (() -> Unit)? = null
) {
    val context = LocalContext.current
    val finalTitle = title.ifEmpty { context.getString(R.string.backend_not_available) }
    val finalDescription = description.ifEmpty { context.getString(R.string.backend_not_available_description) }
    EmptyState(
        icon = Icons.Default.Error,
        title = finalTitle,
        description = finalDescription,
        modifier = modifier,
        onRetry = onRetry
    )
}

/**
 * Root access unavailable state composable.
 */
@Composable
fun RootUnavailableState(
    modifier: Modifier = Modifier
) {
    val context = LocalContext.current
    EmptyState(
        icon = Icons.Default.Error,
        title = context.getString(R.string.root_access_not_available),
        description = context.getString(R.string.root_access_not_available_description),
        modifier = modifier
    )
}

/**
 * The backend is installed but a MUST HAVE probe failed, so no container can
 * run. The Home card carries the link to the kernel guide.
 */
@Composable
fun KernelUnsupportedState(
    modifier: Modifier = Modifier
) {
    val context = LocalContext.current
    EmptyState(
        icon = Icons.Default.Error,
        title = context.getString(R.string.kernel_unsupported),
        description = context.getString(R.string.kernel_unsupported_description),
        modifier = modifier
    )
}
