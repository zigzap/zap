{
  description = "zap dev shell";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixpkgs-unstable";
    flake-utils.url = "github:numtide/flake-utils";

    # required for latest zig
    zig.url = "github:mitchellh/zig-overlay";

    # Used for shell.nix
    flake-compat = {
      url = github:edolstra/flake-compat;
      flake = false;
    };
  };

  outputs = {
    self,
    nixpkgs,
    flake-utils,
    ...
  } @ inputs: let
    overlays = [
      # Other overlays
      (final: prev: {
        zigpkgs = inputs.zig.packages.${prev.system};
      })
    ];

    # Our supported systems are the same supported systems as the Zig binaries
    systems = builtins.attrNames inputs.zig.packages;
  in
    flake-utils.lib.eachSystem systems (
      system: let
        pkgs = import nixpkgs {inherit overlays system; };
      in rec {
        devShells.default = pkgs.mkShell {
          nativeBuildInputs = with pkgs; [
            zigpkgs."0.14.0"
            bat
            wrk

            pkgs.zlib
            pkgs.icu
            pkgs.openssl

            pkgs.neofetch
            pkgs.util-linux    # lscpu
          ];

          buildInputs = with pkgs; [
            # we need a version of bash capable of being interactive
            # as opposed to a bash just used for building this flake
            # in non-interactive mode
            bashInteractive
          ];

          shellHook = ''
            # once we set SHELL to point to the interactive bash, neovim will
            # launch the correct $SHELL in its :terminal
            export SHELL=${pkgs.bashInteractive}/bin/bash
            export LD_LIBRARY_PATH=${pkgs.zlib.out}/lib:${pkgs.icu.out}/lib:${pkgs.openssl.out}/lib:$LD_LIBRARY_PATH
          '';
        };

        devShells.build = pkgs.mkShell {
          nativeBuildInputs = with pkgs; [
            zigpkgs."0.14.0"
            zigpkgs.master
          ];

          buildInputs = with pkgs; [
            # we need a version of bash capable of being interactive
            # as opposed to a bash just used for building this flake
            # in non-interactive mode
            bashInteractive
          ];

          shellHook = ''
            # once we set SHELL to point to the interactive bash, neovim will
            # launch the correct $SHELL in its :terminal
            export SHELL=${pkgs.bashInteractive}/bin/bash
            export LD_LIBRARY_PATH=${pkgs.zlib.out}/lib:${pkgs.icu.out}/lib:${pkgs.openssl.out}/lib:$LD_LIBRARY_PATH
          '';
        };

        devShells.masta = pkgs.mkShell {
          nativeBuildInputs = with pkgs; [
            zigpkgs.master
            pkgs.openssl
          ];

          buildInputs = with pkgs; [
            # we need a version of bash capable of being interactive
            # as opposed to a bash just used for building this flake
            # in non-interactive mode
            bashInteractive
          ];

          shellHook = ''
            # once we set SHELL to point to the interactive bash, neovim will
            # launch the correct $SHELL in its :terminal
            export SHELL=${pkgs.bashInteractive}/bin/bash
            export LD_LIBRARY_PATH=${pkgs.zlib.out}/lib:${pkgs.icu.out}/lib:${pkgs.openssl.out}/lib:$LD_LIBRARY_PATH
          '';
        };

        # For compatibility with older versions of the `nix` binary
        devShell = self.devShells.${system}.default;
      }
    );
}
