{
    pkgs ? import <nixpkgs> { 
        overlays = [ 
            (import (builtins.fetchTarball {
              # url = https://github.com/nix-community/neovim-nightly-overlay/archive/master.tar.gz;
              url = https://github.com/nix-community/neovim-nightly-overlay/archive/72ff8b1ca0331a8735c1eeaefb95c12dfe21d30a.tar.gz;
            }))
        ]; 
    }
} :
pkgs.mkShell {
    nativeBuildInputs = [ pkgs.neovim-nightly pkgs.bat pkgs.wrk];
}
