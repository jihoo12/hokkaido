{
  description = "hokkaido — LLVM-based compiler with Rust cubical backend";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
      in
      {
        packages.default = pkgs.callPackage ./default.nix { };

        devShells.default = pkgs.mkShell {
          inputsFrom = [ self.packages.${system}.default ];
          buildInputs = with pkgs; [ cmake ];
          shellHook = ''
            echo "--- Compiler Development Environment Loaded ---"
            echo "Using CMake and $(llvm-config --version)"
          '';
        };
      });
}
