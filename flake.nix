{
  description = "Droidspaces - High-performance Container Runtime";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/c6e5ca3c836a5f4dd9af9f2c1fc1c38f0fac988a";

    nixpkgs-with-systemd-v259.url = "github:NixOS/nixpkgs/b86751bc4085f48661017fa226dee99fab6c651b";

    nixpkgs-with-systemd-v257.url = "github:NixOS/nixpkgs/3cbe716e2346710d6e1f7c559363d14e11c32a4";

    flake-parts.url = "github:hercules-ci/flake-parts";

    import-tree.url = "github:vic/import-tree";

    artifacts = {
      url = "github:loystonpais/Droidspaces-OSS/artifacts";
      flake = false;
    };

    finix.url = "github:finix-community/finix?ref=main";
  };

  outputs = inputs @ {
    flake-parts,
    nixpkgs,
    ...
  }:
    flake-parts.lib.mkFlake {
      inherit inputs;
      specialArgs = rec {
        version = let
          header = builtins.readFile ./src/droidspace.h;
          match = builtins.match ".*#define DS_VERSION \"([^\"]+)\".*" (lib.replaceStrings ["\n"] [" "] header);
        in
          if match == null
          then "0.0.0"
          else builtins.head match;

        binSource = lib.fileset.toSource {
          root = ./.;
          fileset = lib.fileset.unions [
            ./Makefile
            ./LICENSE
            ./src
          ];
        };

        androidAppSource = ./Android;

        systems = ["x86_64-linux" "aarch64-linux"];

        lib = nixpkgs.lib;
      };
    } (inputs.import-tree ./nix);
}
