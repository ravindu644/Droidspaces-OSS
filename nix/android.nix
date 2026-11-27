{
  version,
  inputs,
  self,
  lib,
  mkDroidspacesAndroidApp,
  androidAppSource,
  ...
}: {
  _module.args = {
    mkDroidspacesAndroidApp = pkgs: androidSdk: baseOverrides: let
      # Gradle's buildBackend task runs `make <arch>` for the phone arches and
      # looks for <triplet>-gcc under MUSL_CROSS, so hand it nixpkgs' musl
      # cross compilers under the names the Makefile expects.
      muslCross = pkgs.linkFarm "droidspaces-musl-cross" (lib.mapAttrsToList (triplet: cross: {
          name = "${triplet}-gcc";
          path = "${cross.stdenv.cc}/bin/${cross.stdenv.cc.targetPrefix}gcc";
        }) {
          "aarch64-linux-musl" = pkgs.pkgsCross.aarch64-multiplatform-musl;
          "arm-linux-musleabihf" = pkgs.pkgsCross.muslpi;
          "x86_64-linux-musl" = pkgs.pkgsCross.musl64;
          "i686-linux-musl" = pkgs.pkgsCross.musl32;
        });
    in
      (pkgs.stdenvNoCC.mkDerivation (finalAttrs: {
        pname = "droidspaces-app";
        inherit version;
        src = androidAppSource;
        sourceRoot = "${androidAppSource.name}/Android";

        nativeBuildInputs = [
          pkgs.jdk17
          pkgs.gradle_8
          pkgs.gnumake
          androidSdk
        ];

        ANDROID_SDK_ROOT = "${androidSdk}/libexec/android-sdk";
        ANDROID_HOME = "${androidSdk}/libexec/android-sdk";
        JAVA_HOME = pkgs.jdk17.home;
        MUSL_CROSS = muslCross;

        mitmCache = pkgs.gradle.fetchDeps {
          pkg = finalAttrs.finalPackage;
          data = builtins.fromJSON (builtins.readFile "${inputs.artifacts}/android-gradle-lockfile.json");
        };

        __darwinAllowLocalNetworking = true;

        gradleFlags = [
          "-Dfile.encoding=utf-8"
          "-Pandroid.aapt2FromMavenOverride=${androidSdk}/libexec/android-sdk/build-tools/35.0.0/aapt2"
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

        # The APK carries the backend it built, signed by the build's own debug key
        installPhase = ''
          find app/build/outputs/apk -name "*.apk" -exec install -Dm644 {} $out/droidspaces.apk \;
        '';
      })).overrideAttrs
      baseOverrides;
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
          # compileSdk and AGP 8.11 in Android/app/build.gradle.kts
          buildToolsVersions = ["35.0.0"];
          platformVersions = ["36"];
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
        pkgs.gnumake
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
