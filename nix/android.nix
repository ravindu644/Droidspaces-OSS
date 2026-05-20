{
  version,
  inputs,
  self,
  mkDroidspacesAndroidApp,
  androidAppSource,
  ...
}: {
  _module.args = {
    mkDroidspacesAndroidApp = pkgs: androidSdk: baseOverrides: let
      muslBuilds = self.legacyPackages.${pkgs.stdenv.hostPlatform.system}.muslBuilds;

      baseApp =
        (pkgs.stdenvNoCC.mkDerivation (finalAttrs: {
          pname = "droidspaces-app-base";
          inherit version;
          src = androidAppSource;

          nativeBuildInputs = [
            pkgs.jdk17
            pkgs.gradle_8
            androidSdk
          ];

          ANDROID_SDK_ROOT = "${androidSdk}/libexec/android-sdk";
          ANDROID_HOME = "${androidSdk}/libexec/android-sdk";
          JAVA_HOME = pkgs.jdk17.home;

          mitmCache = pkgs.gradle.fetchDeps {
            pkg = finalAttrs.finalPackage;
            data = builtins.fromJSON (builtins.readFile "${inputs.artifacts}/android-gradle-lockfile.json");
          };

          __darwinAllowLocalNetworking = true;

          gradleFlags = [
            "-Dfile.encoding=utf-8"
            "-Pandroid.aapt2FromMavenOverride=${androidSdk}/libexec/android-sdk/build-tools/34.0.0/aapt2"
            "-Dkotlin.compiler.execution.strategy=in-process"
          ];

          gradleBuildTask = "assembleRelease";
          gradleUpdateTask = "assembleRelease";

          preBuild = ''
            cat <<EOF >> app/build.gradle.kts

            android {
                lint {
                    checkReleaseBuilds = false
                    abortOnError = false
                }
            }
            EOF
          '';

          installPhase = ''
            find app/build/outputs/apk -name "*.apk" -exec install -Dm644 {} $out/droidspaces.apk \;
          '';
        })).overrideAttrs
        baseOverrides;

      app = pkgs.stdenvNoCC.mkDerivation (finalAttrs: {
        pname = "droidspaces-app";
        version = baseApp.version;

        passthru.baseApp = baseApp;

        nativeBuildInputs = [
          pkgs.zip
          pkgs.jdk17
        ];

        dontUnpack = true;

        buildPhase = ''
          mkdir -p assets/binaries
          cp ${muslBuilds.aarch64}/bin/droidspaces assets/binaries/droidspaces-aarch64
          cp ${muslBuilds.armhf}/bin/droidspaces assets/binaries/droidspaces-armhf
          cp ${muslBuilds.x86_64}/bin/droidspaces assets/binaries/droidspaces-x86_64
          cp ${muslBuilds.x86}/bin/droidspaces assets/binaries/droidspaces-x86

          find ${baseApp} -name "*.apk" -exec cp {} droidspaces-base.apk \;
          chmod +w droidspaces-base.apk

          zip -ur droidspaces-base.apk assets/

          ${androidSdk}/libexec/android-sdk/build-tools/*/zipalign -p -f 4 droidspaces-base.apk droidspaces.apk

          keytool -genkeypair -v -keystore temp.keystore -alias droidspaces -keyalg RSA \
            -keysize 2048 -validity 10000 -storepass android -keypass android -dname "CN=Nix Build, O=Droidspaces"

          ${androidSdk}/libexec/android-sdk/build-tools/34.0.0/apksigner sign --ks \
            temp.keystore --ks-pass pass:android --key-pass pass:android droidspaces.apk
        '';

        installPhase = ''
          install -Dm644 droidspaces.apk $out/droidspaces.apk
        '';
      });
    in
      app;
  };

  perSystem = {
    pkgs,
    androidSdk,
    system,
    ...
  }: {
    _module.args = {
      androidSdk =
        (pkgs.androidenv.composeAndroidPackages {
          buildToolsVersions = ["34.0.0"];
          platformVersions = ["34"];
          abiVersions = ["arm64-v8a" "armeabi-v7a" "x86_64"];
          includeEmulator = false;
          includeSources = false;
          includeSystemImages = false;
          includeExtras = ["extras;google;m2repository" "extras;android;m2repository"];
        }).androidsdk;
    };

    packages.app = self.legacyPackages.${system}.androidApp.release;

    legacyPackages.androidApp = {
      release = mkDroidspacesAndroidApp pkgs androidSdk {};
      debug = mkDroidspacesAndroidApp pkgs androidSdk {
        gradleBuildTask = "assembleDebug";
        gradleUpdateTask = "assembleDebug";
      };
    };

    devShells.app = pkgs.mkShell {
      nativeBuildInputs = [
        pkgs.jdk17
        pkgs.gradle_8
        androidSdk
      ];

      shellHook = ''
        export ANDROID_SDK_ROOT="${androidSdk}/libexec/android-sdk"
        export ANDROID_HOME="${androidSdk}/libexec/android-sdk"
        export JAVA_HOME="${pkgs.jdk17.home}"
      '';
    };
  };
}
