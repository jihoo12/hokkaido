{ pkgs ? import <nixpkgs> {} }:

pkgs.stdenv.mkDerivation {
  pname = "hokkaido";
  version = "0.2.2";

  src = ./.;

  nativeBuildInputs = with pkgs; [
    cmake
    cargo
    rustc
  ];

  buildInputs = with pkgs; [
    llvmPackages.llvm
    llvmPackages.libllvm
  ];

  # cargo needs a writable CARGO_HOME during the build
  preConfigure = ''
    export CARGO_HOME=$TMPDIR/cargo-home
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p $out/bin
    cp hokkaido $out/bin/
    runHook postInstall
  '';

  meta = with pkgs.lib; {
    description = "Hokkaido compiler (LLVM + Rust cubical backend)";
    mainProgram = "hokkaido";
  };
}
