package com.droidspaces.app.util

import androidx.compose.animation.core.*
import androidx.compose.animation.core.tween

/**
 * Centralized animation utilities for consistent app feel.
 * Implements "Premium & Instant" animation system.
 */
object AnimationUtils {
    // Standard durations per user request
    const val DURATION_FAST = 150
    const val DURATION_MEDIUM = 200
    const val DURATION_SLOW = 300

    // Standard Easing
    val STANDARD_EASING = FastOutSlowInEasing

    // Pre-computed Specs
    fun <T> fastSpec(): TweenSpec<T> = tween(
        durationMillis = DURATION_FAST,
        easing = STANDARD_EASING
    )

    fun <T> mediumSpec(): TweenSpec<T> = tween(
        durationMillis = DURATION_MEDIUM,
        easing = STANDARD_EASING
    )

    fun <T> slowSpec(): TweenSpec<T> = tween(
        durationMillis = DURATION_SLOW,
        easing = STANDARD_EASING
    )
}

