{
    description = "Workspace grid overview plugin for Hyprland (gridoverview)";

    inputs = {
        nixpkgs.url = "github:nixos/nixpkgs?ref=nixos-unstable";
        hyprland.url = "github:hyprwm/Hyprland";
        flake-parts = {
            url = "github:hercules-ci/flake-parts";
            inputs.nixpkgs-lib.follows = "nixpkgs";
        };
    };

    outputs = inputs @ {
        self,
        nixpkgs,
        hyprland,
        flake-parts,
        ...
    }:
        flake-parts.lib.mkFlake {inherit inputs;} {
            systems = ["x86_64-linux"];

            perSystem = {
                config,
                pkgs,
                system,
                ...
            }: let
                hyprlandPackage = hyprland.packages.${system}.hyprland;
            in {
                packages.gridoverview = pkgs.hyprlandPlugins.mkHyprlandPlugin {
                    hyprland = hyprlandPackage;
                    pluginName = "gridoverview";
                    version = self.shortRev or self.dirtyShortRev or "unknown";
                    src = ./.;

                    buildInputs = [pkgs.lua5_4];

                    enableParallelBuilding = true;
                    dontUseCmakeConfigure = true;

                    buildPhase = ''
                        runHook preBuild
                        export SCROLLOVERVIEW_BUILD_VERSION="${self.shortRev or self.dirtyShortRev or "unknown"}"
                        make all
                        runHook postBuild
                    '';

                    installPhase = ''
                        runHook preInstall
                        mkdir -p "$out/lib"
                        mv gridoverview.so "$out/lib/libgridoverview.so"
                        runHook postInstall
                    '';

                    meta = {
                        description = "Free workspace grid overview for Hyprland";
                        homepage = "https://github.com/ismay7/hyprland-grid-overview";
                        license = pkgs.lib.licenses.bsd3;
                        platforms = pkgs.lib.platforms.linux;
                    };
                };

                packages.default = config.packages.gridoverview;

                devShells.default = pkgs.mkShell {
                    name = "hyprland-scroll-overview-dev";

                    inputsFrom = [config.packages.gridoverview];

                    nativeBuildInputs = with pkgs; [
                        meson
                        clang-tools
                        ninja
                        pkg-config
                    ];
                };
            };
        };
}
