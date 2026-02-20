{
  description = "DWC - A dynamic Wayland compositor";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = {
    self,
    nixpkgs,
  }: let
    systems = ["x86_64-linux" "aarch64-linux"];
    forAllSystems = fn: nixpkgs.lib.genAttrs systems (system: fn nixpkgs.legacyPackages.${system});
  in {
    devShells = forAllSystems (pkgs: {
      default = pkgs.mkShell {
        buildInputs = with pkgs; [
          gcc
          gnumake
          pkg-config
          bear
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
          echo "DWC development environment"
          echo ""
          echo "  make                - build"
          echo "  bear -- make        - build + compile_commands.json"
          echo "  ./dwc               - run (from TTY)"
        '';
      };
    });

    packages = forAllSystems (pkgs: {
      default = pkgs.stdenv.mkDerivation {
        pname = "dwc";
        version = "0.1.0";
        src = ./.;

        nativeBuildInputs = with pkgs; [
          gnumake
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

        buildPhase = ''
          make
        '';

        installPhase = ''
          mkdir -p $out/bin
          cp dwc $out/bin/
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
