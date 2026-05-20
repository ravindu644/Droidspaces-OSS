{inputs, ...}: {
  flake = {
    nixosModules = {
      pin-systemd-v257 = {pkgs, ...}: let
        pkgs-with-systemd-v257 = import inputs.nixpkgs-with-systemd-v257 {
          inherit (pkgs.stdenv.hostPlatform) system;
        };

        systemd-v257 = pkgs-with-systemd-v257.systemd;

        dummySystemdUnits = pkgs.runCommand "dummy-systemd-units" {} ''
          mkdir -p $out/example/systemd/system

          # Factory reset units
          touch $out/example/systemd/system/factory-reset.target
          touch $out/example/systemd/system/systemd-factory-reset-request.service
          touch $out/example/systemd/system/systemd-factory-reset-reboot.service

          # Journalctl socket (new in recent systemd)
          touch $out/example/systemd/system/systemd-journalctl.socket
          touch $out/example/systemd/system/systemd-journalctl@.service

          # In case these are also missing in v257.9
          touch $out/example/systemd/system/systemd-journald-varlink@.socket

          mkdir -p $out/example/systemd/system/factory-reset.target.wants
        '';
      in {
        systemd.package = pkgs.symlinkJoin {
          name = "systemd-v257-pinned";
          paths = [
            systemd-v257

            dummySystemdUnits
          ];

          passthru =
            (systemd-v257.passthru or {})
            // {
              interfaceVersion = 2;
              withLogind = true;
              withImportd = true;
              withMachined = true;
              withNspawn = true;
              withTpm2Units = false;
              withSysupdate = false;
            };

          meta = systemd-v257.systemd.meta or {};
        };

        systemd.coredump.enable = false;
      };
    };
  };
}
