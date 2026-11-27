package com.droidspaces.app.ui.viewmodel

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.droidspaces.app.util.ContainerSystemdManager
import kotlinx.coroutines.launch

sealed class JournaldState {
    data object Loading : JournaldState()
    data object Error : JournaldState()
    data class Ready(val logs: List<String>) : JournaldState()
}

class JournaldViewModel : ViewModel() {
    var state by mutableStateOf<JournaldState>(JournaldState.Loading)
        private set
    var lineCount by mutableIntStateOf(100)

    fun loadLogs(containerName: String, unitName: String) {
        viewModelScope.launch {
            state = JournaldState.Loading
            val logs = ContainerSystemdManager.dumpJournal(containerName, unitName, lineCount)
            state = if (logs.isNotEmpty()) JournaldState.Ready(logs) else JournaldState.Error
        }
    }
}
