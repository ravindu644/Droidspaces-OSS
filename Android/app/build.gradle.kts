import java.util.Properties
import java.io.FileInputStream
import java.util.Locale

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// Load keystore properties from local.properties or gradle.properties
val keystorePropertiesFile = rootProject.file("local.properties")
val keystoreProperties = Properties()
if (keystorePropertiesFile.exists()) {
    FileInputStream(keystorePropertiesFile).use {
        keystoreProperties.load(it)
    }
}

// Helper to extract version from C header
fun getDSVersion(): String {
    val headerFile = file("../../src/droidspace.h")
    if (!headerFile.exists()) return "1.0.0"
    
    val pattern = Regex("#define\\s+DS_VERSION\\s+\"([^\"]+)\"")
    var foundVersion = "1.0.0"
    
    headerFile.useLines { lines ->
        for (line in lines) {
            val match = pattern.find(line)
            if (match != null) {
                foundVersion = match.groupValues[1]
                break
            }
        }
    }
    return foundVersion
}

val dsVersionName = getDSVersion()
val dsVersionCodeVal = dsVersionName.split(".").let { parts ->
    try {
        val major = parts.getOrNull(0)?.toInt() ?: 1
        val minor = parts.getOrNull(1)?.toInt() ?: 0
        val patch = parts.getOrNull(2)?.toInt() ?: 0
        major * 10000 + minor * 100 + patch
    } catch (e: Exception) {
        1
    }
}

android {
    namespace = "com.droidspaces.app"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.droidspaces.app"
        minSdk = 26
        targetSdk = 34
        versionCode = dsVersionCodeVal
        versionName = dsVersionName

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        vectorDrawables {
            useSupportLibrary = true
        }
    }

    signingConfigs {
        // Use droidspaces.keystore for both debug and release
        // Passwords are loaded from local.properties, gradle.properties, or environment variables
        val keystoreFile = file("../../droidspaces.keystore")
        val keystorePassword = keystoreProperties["KEYSTORE_PASSWORD"] as String? 
            ?: project.findProperty("KEYSTORE_PASSWORD") as String? 
            ?: ""
        val keyAliasName = keystoreProperties["KEY_ALIAS"] as String?
            ?: project.findProperty("KEY_ALIAS") as String?
            ?: "droidspaces"
        val actualKeyPassword = keystoreProperties["KEY_PASSWORD"] as String?
            ?: project.findProperty("KEY_PASSWORD") as String?
            ?: keystorePassword

        if (keystoreFile.exists() && keystorePassword.isNotEmpty()) {
            getByName("debug") {
                storeFile = keystoreFile
                storePassword = keystorePassword
                keyAlias = keyAliasName
                keyPassword = actualKeyPassword
                enableV2Signing = true
                enableV3Signing = true
                enableV4Signing = true
            }
            create("release") {
                storeFile = keystoreFile
                storePassword = keystorePassword
                keyAlias = keyAliasName
                keyPassword = actualKeyPassword
                enableV2Signing = true
                enableV3Signing = true
                enableV4Signing = true
            }
        } else {
            // Fallback to default debug keystore or generate a temporary one
            var fallbackKeystore = file(System.getProperty("user.home") + "/.android/debug.keystore")

            if (!keystoreFile.exists() && !fallbackKeystore.exists()) {
                println("WARNING: No keystore found. Generating temporary CI keystore...")
                val ciKeystore = file("../../ci-debug.keystore")
                if (!ciKeystore.exists()) {
                    val dn = "CN=John Doe, OU=Mobile, O=Anonymized, L=Earth, ST=Galaxy, C=UN"
                    project.exec {
                        commandLine(
                            "keytool", "-genkeypair",
                            "-v",
                            "-keystore", ciKeystore.absolutePath,
                            "-alias", "droidspaces",
                            "-keyalg", "RSA",
                            "-keysize", "2048",
                            "-validity", "10000",
                            "-storetype", "JKS",
                            "-storepass", "android",
                            "-keypass", "android",
                            "-dname", dn,
                            "-sigalg", "SHA256withRSA"
                        )
                    }
                }
                fallbackKeystore = ciKeystore
            }

            println("Using fallback keystore: ${fallbackKeystore.absolutePath}")

            getByName("debug") {
                storeFile = fallbackKeystore
                storePassword = "android"
                keyAlias = if (fallbackKeystore.name.contains("ci-debug")) "droidspaces" else "androiddebugkey"
                keyPassword = "android"
            }
            create("release") {
                initWith(getByName("debug"))
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
            // Enable R8 full mode for maximum optimization
            isDebuggable = false
            isJniDebuggable = false
            signingConfig = signingConfigs.getByName("release")
        }
        debug {
            // Disable minification in debug for faster builds
            isMinifyEnabled = false
            isDebuggable = true
            signingConfig = signingConfigs.getByName("debug")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
        // Aggressive Kotlin compiler optimizations for maximum performance
        freeCompilerArgs += listOf(
            "-opt-in=kotlin.RequiresOptIn",
            "-Xinline-classes", // Enable inline classes for better performance
            "-Xno-param-assertions", // Remove parameter assertions in release builds
            "-Xno-call-assertions", // Remove call assertions in release builds
            "-Xno-receiver-assertions", // Remove receiver assertions in release builds
            "-Xjvm-default=all", // Use JVM default methods for better interop
            "-Xbackend-threads=0" // Use all available threads for compilation
        )
    }

    buildFeatures {
        compose = true
    }

    composeOptions {
        kotlinCompilerExtensionVersion = "1.5.8"
    }

    packaging {
        resources {
            excludes += "/META-INF/{AL2.0,LGPL2.1}"
        }
    }
}

// ---------------------------------------------------------------------------
// Auto-generate language list from res/values-* directories.
//
// This task runs before every build and writes two files:
//   1. src/main/assets/supported_locales.txt  — read by LocaleHelper at
//      runtime to build the in-app language picker dynamically.
//   2. src/main/res/xml/locales_config.xml    — keeps the Android 13+
//      per-app language system setting in sync automatically.
//
// Detection rule: a values-XX directory is a translation if it contains a
// strings.xml file (case-insensitive). This cleanly excludes non-language
// qualifiers like values-night, values-v31, values-v33, etc., which never
// carry strings.xml. No hardcoded exclusion list is needed.
//
// Adding a new language via Weblate: just merge the PR — the next build
// picks it up with zero manual changes required.
// ---------------------------------------------------------------------------
tasks.register("generateSupportedLocalesList") {
    val resDir = file("src/main/res")
    val assetsDir = file("src/main/assets")
    val localesAsset = file("src/main/assets/supported_locales.txt")
    val localesConfig = file("src/main/res/xml/locales_config.xml")

    inputs.dir(resDir)
    outputs.files(localesAsset, localesConfig)

    doLast {
        assetsDir.mkdirs()

        // Scan values-XX dirs; include only those with a strings.xml (any case)
        val localeCodes = resDir.listFiles()
            .orEmpty()
            .filter { dir ->
                dir.isDirectory &&
                dir.name.startsWith("values-") &&
                dir.listFiles().orEmpty().any { f ->
                    f.name.equals("strings.xml", ignoreCase = true)
                }
            }
            .mapNotNull { dir ->
                val suffix = dir.name.removePrefix("values-")
                // Convert Android qualifier format (pt-rBR) → BCP 47 (pt-BR)
                // so java.util.Locale can validate it.
                val bcp47 = suffix.replace(Regex("-r([A-Z])"), "-$1")
                val locale = Locale.forLanguageTag(bcp47)
                // Reject anything that doesn't parse as a real language subtag
                if (locale.language.isNotEmpty()) suffix else null
            }
            .sorted()

        // --- 1. assets/supported_locales.txt ---
        localesAsset.writeText(localeCodes.joinToString("\n") + "\n")
        println("generateSupportedLocalesList: wrote ${localeCodes.size} locales → $localeCodes")

        // --- 2. res/xml/locales_config.xml ---
        val sb = StringBuilder()
        sb.appendLine("""<?xml version="1.0" encoding="utf-8"?>""")
        sb.appendLine("""<locale-config xmlns:android="http://schemas.android.com/apk/res/android">""")
        sb.appendLine("""    <locale android:name="en" />""") // default / fallback
        localeCodes.forEach { code ->
            val bcp47 = code.replace("-r", "-")
            sb.appendLine("""    <locale android:name="$bcp47" />""")
        }
        sb.appendLine("""</locale-config>""")
        localesConfig.writeText(sb.toString())
    }
}

// Run before every build variant
tasks.configureEach {
    if (name.startsWith("pre") && name.endsWith("Build")) {
        dependsOn("generateSupportedLocalesList")
    }
}

dependencies {
    // Compose BOM
    implementation(platform("androidx.compose:compose-bom:2024.02.00"))
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-graphics")
    implementation("androidx.compose.ui:ui-tooling-preview")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.material:material-icons-extended")

    // Core Android
    implementation("androidx.core:core-ktx:1.12.0")
    implementation("androidx.core:core-splashscreen:1.0.1")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.7.0")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.7.0")
    implementation("androidx.lifecycle:lifecycle-process:2.7.0")
    implementation("androidx.activity:activity-compose:1.8.2")
    implementation("androidx.appcompat:appcompat:1.7.0")
    implementation("com.google.android.material:material:1.11.0")

    // Removed App Startup library - direct initialization in Application.onCreate() is faster
    // Eliminates ContentProvider overhead (~5-10ms saved)

    // Navigation
    implementation("androidx.navigation:navigation-compose:2.7.7")

    // ViewModel
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.7.0")

    // Root execution - libsu
    implementation("com.github.topjohnwu.libsu:core:5.2.1")
    implementation("com.github.topjohnwu.libsu:service:5.2.1")
    implementation("com.github.topjohnwu.libsu:io:5.2.1")

    // Coroutines - latest version for better performance
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.7.3")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.7.3")

    // Terminal emulator (ported from LXC-Manager / termux-app)
    implementation("com.github.termux.termux-app:terminal-view:0.118.1")
    implementation("com.github.termux.termux-app:terminal-emulator:0.118.1")

    // Debug
    debugImplementation("androidx.compose.ui:ui-tooling")
    debugImplementation("androidx.compose.ui:ui-test-manifest")
}
