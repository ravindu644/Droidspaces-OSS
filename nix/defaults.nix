{
  inputs,
  lib,
  self,
  systems,
  ...
}: {
  inherit systems;

  perSystem = {system, ...}: {
    _module.args.pkgs = import inputs.nixpkgs {
      inherit system;
      config.allowUnfree = true;
      config.android_sdk.accept_license = true;

      overlays = [self.overlays.default];
    };
  };

  flake.overlays = {
    default = final: prev: {
      droidspacesConfigIf = pattern:
        prev.writeShellScript "config-if" ''
          grep -q ${lib.escapeShellArg pattern} /run/droidspaces/container.config
        '';
    };
  };
}
