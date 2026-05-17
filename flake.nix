{
  description = "Droidspaces - High-performance Container Runtime";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/c6e5ca3c836a5f4dd9af9f2c1fc1c38f0fac988a";

    nixpkgs-with-systemd-v259.url = "github:NixOS/nixpkgs/b86751bc4085f48661017fa226dee99fab6c651b";

    flake-utils.url = "github:numtide/flake-utils";

    artifacts = {
      url = "github:loystonpais/Droidspaces-OSS/artifacts";
      flake = false;
    };

    finix.url = "github:finix-community/finix?ref=main";
  };

  outputs = {
    self,
    nixpkgs,
    flake-utils,
    artifacts,
    ...
  } @ inputs: let
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

          minimal-with-systemd-v259 =
            (inputs.nixpkgs-with-systemd-v259.lib.nixosSystem {
              inherit system;
              modules = [self.nixosModules.working-droidspaces-rootfs-minimal];
            }).config.system.build.tarball;
        });

        finixDroidspacesTarballs = lib.genAttrs systems (system: {
          experimental =
            (inputs.finix.lib.finixSystem {
              inherit (pkgs) lib;
              modules = [
                {
                  nixpkgs.pkgs = inputs.nixpkgs.lib.mkDefault pkgs;
                }
                self.nixosModules.finix-droidspaces-rootfs-experimental
              ];
            }).config.droidspaces.tarball;
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

            finix-rootfs = mkDynamicVM (nixpkgs.lib.nixosSystem {
              inherit system;
              modules = [self.nixosModules.test-system-finix-rootfs];
            });
          });
        in {
          inherit forArch;
          inherit (forArch.${system}) default nixos-rootfs finix-rootfs;
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

        test-system-finix-rootfs = {pkgs, ...}: {
          imports = [self.nixosModules.test-system-base];

          environment.variables.FINIX_ROOTFS = let
            system = pkgs.stdenv.hostPlatform.system;
            tarballPath = "${self.legacyPackages.${system}.finixDroidspacesTarballs.${system}.experimental}";
            file = builtins.elemAt (lib.filesystem.listFilesRecursive "${tarballPath}/tarball") 0;
          in
            file;

          environment.interactiveShellInit = ''
            echo '------'
            echo 'Finix Droidspaces Minimal Rootfs is available at $FINIX_ROOTFS'
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

          # These services are broken in droidspaces container
          systemd.services.nix-channel-init.enable = false;
          systemd.services.firewall.enable = false;
          systemd.services.wpa_supplicant.enable = false;

          networking.firewall.enable = false;

          # Theoretically systemd should detect container environment and not run udev
          #  but with droidspaces' hardware access enabled, it runs regardless.
          services.udev.enable = lib.mkForce false;
          # For some reason, udevd stuff still runs with services.udev.enable
          #  set to false so let's just disable the services directly
          systemd.services.systemd-udevd.enable = lib.mkForce false;
          systemd.services.systemd-udev-settle.enable = lib.mkForce false;
          systemd.services.systemd-udev-trigger.enable = lib.mkForce false;

          nix.settings.experimental-features = ["nix-command" "flakes"];

          systemd.services.NetworkManager.enable = false;
        };

        finix-droidspaces-rootfs-experimental = {pkgs, ...}: {
          imports = with inputs.finix.nixosModules; [
            openssh
            nix-daemon
            sudo
            bash
            sysklogd

            # Set container stuff
            ({
              pkgs,
              lib,
              config,
              ...
            }: {
              options = {
                droidspaces.tarball = lib.mkOption {
                  type = lib.types.path;
                  description = "Path to droidspaces tarball to be extracted and used as rootfs";
                };
              };

              config = {
                boot.kernel.enable = false;
                boot.initrd.enable = false;
                boot.modprobeConfig.enable = false;

                finit.tasks.register-nix-paths = {
                  runlevels = "S";
                  remain = true;
                  pre = pkgs.writeShellScript "register-nix-paths-pre" ''
                    test -f /nix-path-registration || exit 0
                  '';
                  command = pkgs.writeShellScript "register-nix-paths" ''
                    ${lib.getExe' config.services.nix-daemon.package.out "nix-store"} --load-db < /nix-path-registration
                    rm /nix-path-registration
                    ${lib.getExe' config.services.nix-daemon.package.out "nix-env"} -p /nix/var/nix/profiles/system --set /run/current-system
                  '';
                  description = "Register Nix Store Paths";
                };

                droidspaces.tarball = pkgs.callPackage "${inputs.nixpkgs}/nixos/lib/make-system-tarball.nix" {
                  fileName = "rootfs";
                  extraArgs = "--owner=0";

                  storeContents = [
                    {
                      object = config.system.build.toplevel;
                      symlink = "none";
                    }
                  ];

                  contents = [
                    {
                      source = pkgs.writeShellScript "init" ''
                        systemConfig=${config.system.build.toplevel}

                        export HOME=/root PATH=${lib.makeBinPath [pkgs.coreutils pkgs.util-linux]}

                        echo "starting container..."

                        # Required by the activation script
                        install -m 0755 -d /etc
                        if [ ! -h "/etc/nixos" ]; then
                            install -m 0755 -d /etc/nixos
                        fi
                        install -m 01777 -d /tmp

                        echo "running activation script..."
                        $systemConfig/activate


                        echo "starting finix..."
                        exec ${config.system.build.toplevel}/init "$@"
                      '';
                      target = "/sbin/init";
                    }
                  ];

                  extraCommands = "mkdir -p proc sys dev";
                };
              };
            })
          ];

          services.sysklogd.enable = true;

          services.nix-daemon.enable = true;
          services.nix-daemon.nrBuildUsers = 32;
          services.nix-daemon.settings = {
            experimental-features = [
              "nix-command"
              "flakes"
            ];

            trusted-users = [
              "root"
              "@wheel"
            ];
          };

          services.openssh.enable = true;

          programs.sudo.enable = true;
          programs.bash.enable = true;

          users.users.test = {
            isNormalUser = true;

            extraGroups = [
              "input"
              "video"
              "wheel"
            ];
          };

          environment.systemPackages = with pkgs; [
            nano
            htop
          ];
        };
      };
    };
}
