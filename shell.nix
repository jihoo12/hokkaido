{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  nativeBuildInputs = with pkgs; [
    cmake
    pkg-config
    clang
    cargo
    rustc
  ];

  buildInputs = with pkgs; [
    llvmPackages.llvm
    llvmPackages.libllvm
  ];

  shellHook = ''
    echo "--- Compiler Development Environment Loaded ---"
    echo "Using CMake and $(llvm-config --version)"
  '';
}