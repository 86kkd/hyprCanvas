{
  description = "hyprCanvas";

  inputs.hyprland.url = "github:hyprwm/Hyprland";

  outputs =
    { self, hyprland }:
    let
      inherit (hyprland.inputs) nixpkgs;
      inherit (nixpkgs) lib;

      allSystems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
      forAllSystems =
        func:
        lib.genAttrs allSystems (
          system:
          let
            pkgs = nixpkgs.legacyPackages.${system};
            hyprlandPkg = hyprland.packages.${system}.hyprland;
            hyprCanvasPkg = self.packages.${system}.hyprCanvas;
          in
          func { inherit pkgs hyprlandPkg hyprCanvasPkg; }
        );

      inherit (builtins) substring;
      mkDate =
        date:
        (lib.concatStringsSep "-" [
          (substring 0 4 date)
          (substring 4 2 date)
          (substring 6 2 date)
        ]);
      version = "date=${mkDate (self.lastModifiedDate or "19700101")}_${self.shortRev or "dirty"}";
    in
    {
      packages = forAllSystems (
        {
          pkgs,
          hyprlandPkg,
          hyprCanvasPkg,
        }:
        {
          hyprCanvas = pkgs.stdenv.mkDerivation {
            pname = "hyprCanvas";
            inherit version;
            src = self;

            nativeBuildInputs = [
              pkgs.cmake
              pkgs.pkg-config
              hyprlandPkg
            ];
            buildInputs = [ hyprlandPkg ] ++ hyprlandPkg.buildInputs;

            buildPhase = ''
              make all
            '';

            installPhase = ''
              mkdir -p $out/lib
              cp ./hyprCanvas.so $out/lib/libhyprCanvas.so
            '';

            meta = with lib; {
              homepage = "https://github.com/86kkd/hyprCanvas";
              description = "Hyprland layout plugin providing a scrolling layout like PaperWM";
              license = licenses.mit;
              platforms = platforms.linux;
            };
          };
          default = hyprCanvasPkg;
        }
      );

      devShells = forAllSystems (
        {
          pkgs,
          hyprlandPkg,
          hyprCanvasPkg,
        }:
        {
          default = pkgs.mkShell {
            shellHook = ''
              make dev
            '';
            name = "hyprCanvas-shell";
            nativeBuildInputs = with pkgs; [ cmake ];
            buildInputs = [ hyprlandPkg ];
            inputsFrom = [
              hyprlandPkg
              hyprCanvasPkg
            ];
          };
        }
      );

      formatter = forAllSystems ({ pkgs, ... }: pkgs.nixfmt-rfc-style);
    };
}
