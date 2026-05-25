{
  version,
  lib,
  mkDroidspacesPackage,
  binSource,
  ...
}: {
  _module.args = {
    mkDroidspacesPackage = pkgs: hostPkgs:
      hostPkgs.stdenv.mkDerivation {
        pname = "droidspaces";
        inherit version;
        src = binSource;

        nativeBuildInputs = [pkgs.gnumake];

        enableParallelBuilding = true;

        makeFlags = ["droidspaces"];

        installPhase = ''
          install -Dm755 output/droidspaces $out/bin/droidspaces
        '';

        meta.mainProgram = "droidspaces";
      };
  };

  perSystem = {pkgs, ...}: {
    packages.default = mkDroidspacesPackage pkgs pkgs.pkgsMusl;

    legacyPackages.muslBuilds = {
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

    devShells.default = pkgs.mkShell {
      nativeBuildInputs = [pkgs.gnumake pkgs.pkgsMusl.stdenv.cc];
    };
  };
}
