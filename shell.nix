{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {

  buildInputs = with pkgs; [
    llvmPackages.llvm
    llvmPackages.libllvm
  ];

  shellHook = ''
    echo "--- Compiler Development Environment Loaded ---"
    echo "Using CMake and $(llvm-config --version)"
  '';
}
