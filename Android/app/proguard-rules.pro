# Add project specific ProGuard rules here.
# You can control the set of applied configuration files using the
# proguardFiles setting in build.gradle.kts.

# Keep libsu classes
-keep class com.topjohnwu.superuser.** { *; }
-dontwarn com.topjohnwu.superuser.**

# Keep Compose
-keep class androidx.compose.** { *; }
-dontwarn androidx.compose.**

# Aggressive performance optimizations
-optimizationpasses 2
-dontusemixedcaseclassnames
-dontskipnonpubliclibraryclasses
-allowaccessmodification
-optimizations !code/simplification/arithmetic,!code/simplification/cast,!field/*,!class/merging/*
-verbose

# Remove logging in release builds for performance
-assumenosideeffects class android.util.Log {
    public static *** d(...);
    public static *** v(...);
    public static *** i(...);
}

# Keep Application class
-keep class com.droidspaces.app.DroidspacesApplication { *; }

# Performance: Keep inline functions (they're optimized by compiler)
# Note: R8 may warn about <methods> pattern, but this is valid ProGuard syntax
-keepclassmembers class * {
    inline <methods>;
}

# Performance: Keep @JvmStatic methods for faster access
-keepclassmembers class * {
    @kotlin.jvm.JvmStatic <methods>;
}

# Performance: Keep value classes (inline classes) for zero-allocation
-keep class * implements kotlin.jvm.internal.InlineMarker { *; }

