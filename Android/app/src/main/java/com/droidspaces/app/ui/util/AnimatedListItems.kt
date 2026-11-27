package com.droidspaces.app.ui.util

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.MutableTransitionState
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.padding
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.droidspaces.app.util.AnimationUtils

/** One card in an animated list. [item] is the last data seen, so a removed card can still draw while it leaves. */
class AnimatedEntry<T>(val key: Any, item: T, visible: Boolean) {
    var item by mutableStateOf(item)
        internal set
    val state = MutableTransitionState(visible).apply { targetState = true }
}

/**
 * The live [items] plus the removed ones still playing their exit, each kept where it
 * was. Lists of cards animate through height rather than Modifier.animateItem(): its
 * placement spec also animates every card pushed by a neighbour's size change, so the
 * cards below an opening drawer chase its edge and overlap it.
 *
 * Cards present on the first composition start shown, so nothing animates on launch.
 */
@Composable
fun <T> rememberAnimatedEntries(items: List<T>, key: (T) -> Any): List<AnimatedEntry<T>> {
    val holder = remember { EntryHolder<T>() }
    return holder.update(items, key)
}

private class EntryHolder<T> {
    private var entries: List<AnimatedEntry<T>> = emptyList()
    private var first = true

    fun update(items: List<T>, key: (T) -> Any): List<AnimatedEntry<T>> {
        val old = entries.associateBy { it.key }
        val merged = items.mapTo(mutableListOf()) { item ->
            val k = key(item)
            old[k]?.also { it.item = item; it.state.targetState = true }
                ?: AnimatedEntry(k, item, visible = first)
        }
        first = false
        val live = merged.mapTo(HashSet()) { it.key }

        // A removed card stays right after whatever preceded it, until its exit ends.
        // Reading the transition state here recomposes us when it does, which prunes it.
        entries.forEachIndexed { i, e ->
            if (e.key in live) return@forEachIndexed
            e.state.targetState = false
            if (e.state.isIdle && !e.state.currentState) return@forEachIndexed
            val before = entries.subList(0, i).lastOrNull { it in merged }
            merged.add(if (before == null) 0 else merged.indexOf(before) + 1, e)
        }
        entries = merged
        return merged
    }
}

/**
 * Grows a card in from the top and shrinks it out the same way, with the drawer's timing.
 * The gap below each card lives inside it, so the gap collapses with the card instead of
 * snapping shut after it. Lists using this drop their spacedBy().
 */
@Composable
fun AnimatedListEntry(entry: AnimatedEntry<*>, modifier: Modifier = Modifier, content: @Composable () -> Unit) {
    AnimatedVisibility(
        visibleState = entry.state,
        modifier = modifier,
        enter = expandVertically(AnimationUtils.mediumSpec(), expandFrom = Alignment.Top) + fadeIn(AnimationUtils.mediumSpec()),
        exit = shrinkVertically(AnimationUtils.mediumSpec(), shrinkTowards = Alignment.Top) + fadeOut(AnimationUtils.mediumSpec())
    ) {
        Box(Modifier.padding(bottom = 16.dp)) { content() }
    }
}
