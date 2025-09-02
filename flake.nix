{
  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
  };

  outputs = { nixpkgs, ... }:
    let
      system = "x86_64-linux";

      pkgs = import nixpkgs { inherit system; };
    in
    {
      devShells.${system}.default = pkgs.mkShell {
        nativeBuildInputs = with pkgs; [
          git
          ccls # lsp
          # clang-tools # formatter and lsp
          platformio-core
        ];

        shellHook = ''
          # create compile_commands.json automatically
          pio run --target compiledb

          # alias format='x'
        '';
      };
    };
}
