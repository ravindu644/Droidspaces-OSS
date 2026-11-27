package com.droidspaces.app.ui.component

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import com.droidspaces.app.ui.component.DsDialog
import com.droidspaces.app.R

@Composable
fun HardwareAccessDialog(
    onConfirm: () -> Unit,
    onDismiss: () -> Unit
) {
    val context = LocalContext.current

    var confirmText by remember { mutableStateOf("") }
    val isConfirmed = confirmText == context.getString(R.string.i_understand_caps)

    DsDialog(
        onDismiss = onDismiss,
        footer = {
            DialogFooterRow(
                dismissLabel = context.getString(R.string.cancel),
                confirmLabel = context.getString(R.string.ok),
                onDismiss = onDismiss,
                onConfirm = onConfirm,
                confirmEnabled = isConfirmed,
                destructive = true
            )
        }
    ) {
        Text(
            text = context.getString(R.string.hardware_access),
            style = MaterialTheme.typography.titleLarge,
            fontWeight = FontWeight.Bold
        )

        DangerousWarningCard(
            title = context.getString(R.string.privileged_warning_title),
            text = context.getString(R.string.hw_access_disclaimer)
        )

        ConfirmPhraseField(
            value = confirmText,
            onValueChange = { confirmText = it },
            isError = confirmText.isNotEmpty() && !isConfirmed
        )
    
    }
}
