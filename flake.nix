{
  description = "Droidspaces - High-performance Container Runtime";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/b86751bc4085f48661017fa226dee99fab6c651b";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = {
    self,
    nixpkgs,
    flake-utils,
  }: let
    lib = nixpkgs.lib;
    systems = ["x86_64-linux" "aarch64-linux"];

    mkDroidspacesPackage = pkgs: hostPkgs: let
      fs = lib.fileset;
      source = fs.toSource {
        root = ./.;
        fileset = fs.unions [
          ./Makefile
          ./LICENSE
          ./src
        ];
      };
      version = let
        header = builtins.readFile ./src/droidspace.h;
        match = builtins.match ".*#define DS_VERSION \"([^\"]+)\".*" (lib.replaceStrings ["\n"] [" "] header);
      in
        if match == null
        then "0.0.0"
        else builtins.head match;
    in
      hostPkgs.stdenv.mkDerivation {
        pname = "droidspaces";
        inherit version;
        src = source;

        nativeBuildInputs = [pkgs.gnumake];

        buildPhase = ''
          make -j$NIX_BUILD_CORES droidspaces
        '';

        installPhase = ''
          mkdir -p $out/bin
          cp output/droidspaces $out/bin/
        '';

        meta.mainProgram = "droidspaces";
      };
  in
    flake-utils.lib.eachSystem systems (system: let
      pkgs = import nixpkgs {
        inherit system;
        config.allowUnfree = true;
      };

      # Sets ram and cpu dynamically
      mkDynamicVM = nixos:
        pkgs.writeShellScriptBin "run-${nixos.config.networking.hostName}" ''
          CORES=$(nproc)
          VM_CORES=$((CORES / 2))
          ((VM_CORES < 1)) && VM_CORES=1

          TOTAL_KB=$(grep MemTotal /proc/meminfo | awk '{print $2}')
          VM_RAM_MB=$((TOTAL_KB / 1024 / 2))
          ((VM_RAM_MB < 512)) && VM_RAM_MB=512

          export QEMU_OPTS="-m ''${VM_RAM_MB}M -smp $VM_CORES $QEMU_OPTS"
          echo "Starting VM with $VM_CORES cores and ''${VM_RAM_MB}MB RAM..."
          exec ${nixos.config.system.build.vm}/bin/run-test-vm "$@"
        '';
    in {
      packages.default = mkDroidspacesPackage pkgs pkgs.pkgsMusl;

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
              modules = [self.nixosModules.test-system-nixos-roots];
            });
          });
        in {
          inherit forArch;
          inherit (forArch.${system}) default nixos-rootfs;
        };
      };

      devShells.default = pkgs.mkShell {
        nativeBuildInputs = [pkgs.gnumake pkgs.pkgsMusl.stdenv.cc];
      };
    })
    // {
      nixosModules = {
        test-system-nixos-roots = {pkgs, ...}: {
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
