# This is a basic Nix shell for C development
{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  # Set the shell environment to include necessary tools
  nativeBuildInputs = [
    pkgs.gcc       # GCC compiler
    pkgs.gnumake      # Make utility
    pkgs.gdb       # GDB for debugging
  ];
  buildInputs = [
  pkgs.raylib
  pkgs.raygui
  pkgs.readline
  ];

  # Optionally, you can set environment variables
  shellHook = ''
    export CC=gcc
    export CFLAGS="-Wall -g"
  '';
}

