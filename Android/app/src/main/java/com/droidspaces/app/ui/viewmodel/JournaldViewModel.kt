package com.droidspaces.app.ui.viewmodel

import android.app.Application
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.droidspaces.app.util.ContainerSystemdManager
import kotlinx.coroutines.launch

sealed class JournaldState {
    data object Loading : JournaldState()
    data object Error : JournaldState()
    data class Ready(val logs: List<String>) : JournaldState()
}

class JournaldViewModel(application: Application) : AndroidViewModel(application) {
    var state by mutableStateOf<JournaldState>(JournaldState.Loading)
        private set

    fun loadLogs(containerName: String, unitName: String, lines: Int) {
        viewModelScope.launch {
            state = JournaldState.Loading
            val logs = ContainerSystemdManager.dumpJournal(containerName, unitName, getApplication(), lines)
            state = if (logs.isNotEmpty()) {
                JournaldState.Ready(logs)
            } else {
                JournaldState.Error
            }
        }
    }
}
