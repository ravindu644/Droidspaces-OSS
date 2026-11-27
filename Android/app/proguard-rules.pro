# R8 reads these on top of proguard-android-optimize.txt. Compose and libsu ship
# their own consumer rules, so nothing here keeps them whole; the Application
# class is kept by the manifest.

# Verbose logging is dead weight in a release build.
-assumenosideeffects class android.util.Log {
    public static *** d(...);
    public static *** v(...);
    public static *** i(...);
}
