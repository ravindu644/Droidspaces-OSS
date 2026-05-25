{
  lib,
  systems,
  inputs,
  self,
  ...
}: {
  perSystem = {
    pkgs,
    mkDynamicVM,
    system,
    ...
  }: {
    _module.args = {
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
    };

    legacyPackages = {
      manualTestVMs = let
        forArch = lib.genAttrs systems (system: {
          default = mkDynamicVM (inputs.nixpkgs.lib.nixosSystem {
            inherit system;
            modules = [self.nixosModules.test-system-base];
          });

          nixos-rootfs = mkDynamicVM (inputs.nixpkgs.lib.nixosSystem {
            inherit system;
            modules = [self.nixosModules.test-system-nixos-rootfs];
          });

          finix-rootfs = mkDynamicVM (inputs.nixpkgs.lib.nixosSystem {
            inherit system;
            modules = [self.nixosModules.test-system-finix-rootfs];
          });
        });
      in {
        inherit forArch;
        inherit (forArch.${system}) default nixos-rootfs finix-rootfs;
      };
    };
  };

  flake = {
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
    };
  };
}
