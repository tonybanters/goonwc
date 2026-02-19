{
  description = "DWC - A dynamic Wayland compositor";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    owl = {
      url = "github:tonybanters/owl";
      flake = false;
    };
  };

  outputs = {
    self,
    nixpkgs,
    owl,
  }: let
    systems = ["x86_64-linux" "aarch64-linux"];
    forAllSystems = fn: nixpkgs.lib.genAttrs systems (system: fn nixpkgs.legacyPackages.${system});
  in {
    devShells = forAllSystems (pkgs: {
      default = pkgs.mkShell {
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

        OWL_PATH = "${owl}";

        shellHook = ''
          echo "DWC development environment loaded!"
          echo ""
          echo "  zig build     - Build the compositor"
          echo "  ./zig-out/bin/dwc - Run (from TTY)"
          echo ""
          echo "owl source: $OWL_PATH"
        '';
      };
    });

    packages = forAllSystems (pkgs: {
      default = pkgs.stdenv.mkDerivation {
        pname = "dwc";
        version = "0.1.0";

        src = ./.;

        nativeBuildInputs = with pkgs; [
          zig
          pkg-config
          wayland-scanner
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

        OWL_PATH = "${owl}";

        buildPhase = ''
          export HOME=$(mktemp -d)
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
    });
  };
}
