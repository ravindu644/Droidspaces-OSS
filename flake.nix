{
  description = "Droidspaces - High-performance Container Runtime";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/b86751bc4085f48661017fa226dee99fab6c651b";
    flake-utils.url = "github:numtide/flake-utils";

    artifacts = {
      url = "github:loystonpais/Droidspaces-OSS/artifacts";
      flake = false;
    };
  };

  outputs = {
    self,
    nixpkgs,
    flake-utils,
    artifacts,
  }: let
    lib = nixpkgs.lib;
    systems = ["x86_64-linux" "aarch64-linux"];

    version = let
      header = builtins.readFile ./src/droidspace.h;
      match = builtins.match ".*#define DS_VERSION \"([^\"]+)\".*" (lib.replaceStrings ["\n"] [" "] header);
    in
      if match == null
      then "0.0.0"
      else builtins.head match;

    mkDroidspacesPackage = pkgs: hostPkgs: let
      source = lib.fileset.toSource {
        root = ./.;
        fileset = lib.fileset.unions [
          ./Makefile
          ./LICENSE
          ./src
        ];
      };
    in
      hostPkgs.stdenv.mkDerivation {
        pname = "droidspaces";
        inherit version;
        src = source;

        nativeBuildInputs = [pkgs.gnumake];

        enableParallelBuilding = true;

        makeFlags = ["droidspaces"];

        installPhase = ''
          install -Dm755 output/droidspaces $out/bin/droidspaces
        '';

        meta.mainProgram = "droidspaces";
      };

    mkDroidspacesAndroidApp = pkgs: androidSdk:
      pkgs.stdenv.mkDerivation (finalAttrs: {
        pname = "droidspaces-app";
        inherit version;
        src = ./Android;

        nativeBuildInputs = [
          pkgs.jdk17
          pkgs.gradle_8
          androidSdk
        ];

        # Environment variables for Android build
        ANDROID_SDK_ROOT = "${androidSdk}/libexec/android-sdk";
        ANDROID_HOME = "${androidSdk}/libexec/android-sdk";
        JAVA_HOME = pkgs.jdk17.home;

        mitmCache = pkgs.gradle.fetchDeps {
          pkg = finalAttrs.finalPackage;
          data = builtins.fromJSON (builtins.readFile "${artifacts}/android-gradle-lockfile.json");
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
      });
  in
    flake-utils.lib.eachSystem systems (system: let
      pkgs = import nixpkgs {
        inherit system;
        config.allowUnfree = true;
        config.android_sdk.accept_license = true;
      };

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

      # Sets ram and cpu dynamically
      mkDynamicVM = nixos:
        pkgs.writeShellScriptBin "run-${nixos.config.networking.hostName}" ''
          PATH="$PATH:${lib.makeBinPath (with pkgs; [coreutils gnugrep gawk])}"

          CORES=$(nproc)
          VM_CORES=$((CORES / 2))
          ((VM_CORES < 1)) && VM_CORES=1

          TOTAL_KB=$(grep MemTotal /proc/meminfo | awk '{print $2}')
          VM_RAM_MB=$((TOTAL_KB / 1024 / 2))
          ((VM_RAM_MB < 512)) && VM_RAM_MB=512

          export QEMU_OPTS="-m ''${VM_RAM_MB}M -smp $VM_CORES $QEMU_OPTS"
          echo "Starting VM with $VM_CORES cores and ''${VM_RAM_MB}MB RAM..."
          exec ${nixos.config.system.build.vm}/bin/run-*-vm "$@"
        '';
    in {
      packages.default = mkDroidspacesPackage pkgs pkgs.pkgsMusl;

      packages.app = self.legacyPackages.${system}.androidApp.release;

      legacyPackages = {
        muslBuilds = {
          aarch64 = mkDroidspacesPackage pkgs pkgs.pkgsCross.aarch64-multiplatform-musl;
          x86_64 = mkDroidspacesPackage pkgs pkgs.pkgsCross.musl64;
          armhf = mkDroidspacesPackage pkgs pkgs.pkgsCross.muslpi;
          x86 = mkDroidspacesPackage pkgs pkgs.pkgsCross.musl32;
          riscv64 = mkDroidspacesPackage pkgs pkgs.pkgsCross.riscv64-musl;

          # Experimental
          ppc64 = lib.warn "ppc64 support is experimental" ((mkDroidspacesPackage pkgs pkgs.pkgsCross.ppc64-musl).overrideAttrs {
            NIX_CFLAGS_COMPILE = "-Wno-overflow";
          });
          ppc64le = lib.warn "ppc64le support is experimental" ((mkDroidspacesPackage pkgs pkgs.pkgsCross.musl-power).overrideAttrs {
            NIX_CFLAGS_COMPILE = "-Wno-overflow";
          });
        };

        nixosDroidspacesTarballs = lib.genAttrs systems (system: {
          minimal =
            (nixpkgs.lib.nixosSystem {
              inherit system;
              modules = [self.nixosModules.working-droidspaces-rootfs-minimal];
            }).config.system.build.tarball;
        });

        manualTestVMs = let
          forArch = lib.genAttrs systems (system: {
            default = mkDynamicVM (nixpkgs.lib.nixosSystem {
              inherit system;
              modules = [self.nixosModules.test-system-base];
            });

            nixos-rootfs = mkDynamicVM (nixpkgs.lib.nixosSystem {
              inherit system;
              modules = [self.nixosModules.test-system-nixos-rootfs];
            });
          });
        in {
          inherit forArch;
          inherit (forArch.${system}) default nixos-rootfs;
        };

        androidApp = {
          release = mkDroidspacesAndroidApp pkgs androidSdk;
          debug = (mkDroidspacesAndroidApp pkgs androidSdk).overrideAttrs {
            gradleBuildTask = "assembleDebug";
            gradleUpdateTask = "assembleDebug";
          };
        };
      };

      devShells.default = pkgs.mkShell {
        nativeBuildInputs = [pkgs.gnumake pkgs.pkgsMusl.stdenv.cc];
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
    })
    // {
      nixosModules = {
        test-system-nixos-rootfs = {pkgs, ...}: {
          imports = [self.nixosModules.test-system-base];

          environment.variables.NIXOS_ROOTFS = let
            system = pkgs.stdenv.hostPlatform.system;
            tarballPath = "${self.legacyPackages.${system}.nixosDroidspacesTarballs.${system}.minimal}";
            file = builtins.elemAt (lib.filesystem.listFilesRecursive "${tarballPath}/tarball") 0;
          in
            file;
          environment.interactiveShellInit = ''
            echo '------'
            echo 'NixOS Droidspaces Minimal Rootfs is available at $NIXOS_ROOTFS'
            echo '------'
          '';
        };

        test-system-base = {pkgs, ...}: {
          system.stateVersion = "26.05";
          networking.hostName = "test";

          environment.systemPackages = with pkgs; [
            self.packages.${pkgs.stdenv.hostPlatform.system}.default
            pciutils
            kmod
            iproute2
            wget
            file
            tmux
          ];

          users.users.root.initialPassword = "";
          users.users.tester = {
            isNormalUser = true;
            extraGroups = ["wheel"];
            initialPassword = "";
          };

          security.sudo.wheelNeedsPassword = false;
          services.getty.autologinUser = "tester";

          programs.zsh.enable = true;
          programs.bash.enable = true;

          virtualisation.vmVariant = {
            virtualisation.graphics = false;
            virtualisation.diskSize = 8192;
          };

          virtualisation.vmVariantWithBootLoader = {
            virtualisation.graphics = false;
            virtualisation.diskSize = 8192;
          };

          environment.interactiveShellInit = ''
            alias ds='droidspaces'

            echo '------'
            echo "Manual Test System for droidspaces"
            echo "droidspaces is aliased to ds for ease"
            echo '------'
          '';
        };

        # Minimal configuration that doesn't cause systemd degradation
        working-droidspaces-rootfs-minimal = {modulesPath, ...}: {
          imports = [
            "${modulesPath}/virtualisation/lxc-container.nix"
          ];

          # These services are broken or unnecessary in droidspaces container
          systemd.services.nix-channel-init.enable = false;
          systemd.services.firewall.enable = false;
          systemd.services.wpa_supplicant.enable = false;

          networking.firewall.enable = false;

          # Theoretically systemd should detect container environment and not run udev
          # but we will disable it anyways
          services.udev.enable = false;

          nix.settings.experimental-features = ["nix-command" "flakes"];

          system.stateVersion = "26.05";
        };
      };
    };
}
