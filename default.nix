(import <nixpkgs> {}).callPackage (

{ stdenv, lib }:
stdenv.mkDerivation {
  name = "hidethestuff";
  src = ./hidethestuff.c;
  unpackPhase = ":";
  buildPhase = ''
    $CC $src -Wall -o hidethestuff
  '';
  installPhase = ''
    mkdir -p $out/bin
    cp hidethestuff $out/bin
  '';
}
) {}
