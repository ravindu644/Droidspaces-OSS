package com.droidspaces.app.ui.viewmodel

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.droidspaces.app.ui.screen.InitServiceRow
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch

sealed class InitScreenState {
    data object Loading : InitScreenState()
    data object NotAvailable : InitScreenState()
    data class Ready(val rows: List<InitServiceRow>) : InitScreenState()
}

/**
 * List state for the shared init-system services screen (systemd / OpenRC / procd).
 * Scoped to the screen's back stack entry, so opening a child screen (unit
 * inspection, journal, override editor) and coming back keeps the fetched rows,
 * the search text and the selected filter instead of refetching from a root shell.
 */
class InitServiceViewModel : ViewModel() {
    var screenState by mutableStateOf<InitScreenState>(InitScreenState.Loading)
        private set
    var selectedFilterId by mutableStateOf("")
    var searchQuery by mutableStateOf("")

    private var started = false
    private var fetchJob: Job? = null

    /**
     * First-composition fetch. Guarded by [started] rather than by the state so a
     * NotAvailable result is not silently retried every time the screen comes back.
     * The cached rows keep the navigation lambdas from that first composition; they
     * close over the NavHostController, which lives as long as the graph, so that is fine.
     */
    fun loadOnce(
        defaultFilterId: String,
        isAvailable: suspend (String) -> Boolean,
        fetchRows: suspend (String) -> List<InitServiceRow>,
        containerName: String,
    ) {
        if (started) return
        started = true
        selectedFilterId = defaultFilterId
        fetchServices(containerName, isAvailable, fetchRows)
    }

    fun fetchServices(
        containerName: String,
        isAvailable: suspend (String) -> Boolean,
        fetchRows: suspend (String) -> List<InitServiceRow>,
    ) {
        fetchJob?.cancel()
        screenState = InitScreenState.Loading
        fetchJob = viewModelScope.launch {
            try {
                if (!isAvailable(containerName)) {
                    screenState = InitScreenState.NotAvailable
                    return@launch
                }
                screenState = InitScreenState.Ready(fetchRows(containerName))
            } catch (e: Exception) {
                if (e is CancellationException) throw e
                screenState = InitScreenState.NotAvailable
            }
        }
    }

    fun cancelFetch() {
        fetchJob?.cancel()
    }
}
