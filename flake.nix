{
  description = "DWC - A dynamic Wayland compositor";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in
      {
        devShells.default = pkgs.mkShell {
          buildInputs = with pkgs; [
            zig
            pkg-config
            gdb

            wayland
            wayland-protocols
            wayland-scanner

            libxkbcommon
            libdrm
            libGL
            libinput
            udev
            libgbm
          ];

          shellHook = ''
            echo "DWC development environment loaded!"
            echo ""
            echo "  zig build     - Build the compositor"
            echo "  ./zig-out/bin/dwc - Run (from TTY)"
          '';
        };

        packages.default = pkgs.stdenv.mkDerivation {
          pname = "dwc";
          version = "0.1.0";

          src = ./.;

          nativeBuildInputs = with pkgs; [
            zig
            pkg-config
          ];

          buildInputs = with pkgs; [
            wayland
            libxkbcommon
            libdrm
            libGL
            libinput
            udev
            libgbm
          ];

          buildPhase = ''
            zig build -Doptimize=ReleaseSafe
          '';

          installPhase = ''
            mkdir -p $out/bin
            cp zig-out/bin/dwc $out/bin/
          '';

          meta = with pkgs.lib; {
            description = "A dynamic Wayland compositor";
            license = licenses.gpl3;
            platforms = platforms.linux;
          };
        };
      }
    );
}
