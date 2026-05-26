{
  inputs,
  self,
  lib,
  systems,
  ...
}: {
  perSystem = {...}: {
    legacyPackages = {
      finixDroidspacesTarballs = lib.genAttrs systems (system: let
        pkgs = import inputs.nixpkgs {inherit system;};
      in {
        experimental =
          (inputs.finix.lib.finixSystem {
            inherit (inputs.nixpkgs) lib;
            modules = [
              {
                nixpkgs.pkgs = inputs.nixpkgs.lib.mkDefault pkgs;
              }
              self.nixosModules.finix-droidspaces-rootfs-experimental
            ];
          }).config.droidspaces.tarball;
      });
    };
  };

  flake = {
    nixosModules = {
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

                  {
                    source = config.environment.etc.os-release.source;
                    target = "/etc/os-release";
                  }
                ];

                extraCommands = pkgs.writeShellScript "extra-commands" ''
                  mkdir -p proc sys dev

                  mkdir -p bin && ln -sf ${lib.getExe pkgs.bashInteractive} bin/sh
                '';
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
          fastfetch
        ];
      };
    };
  };
}
