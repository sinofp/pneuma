{
  description = "RISC-V C++ cross-compilation environment";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" "aarch64-darwin" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f system);
    in {
      devShells = forAllSystems (system:
        let pkgs = nixpkgs.legacyPackages.${system};
        in {
          default = pkgs.mkShell {
            buildInputs = with pkgs; [
              pkgsCross.riscv64.stdenv.cc
              (qemu.override {
                hostCpuTargets = [ "riscv64-softmmu" ];
                minimal = true;
                gtkSupport = false;
                sdlSupport = false;
                pulseSupport = false;
                pipewireSupport = false;
                jackSupport = false;
                spiceSupport = false;
                smartcardSupport = false;
                libiscsiSupport = false;
                tpmSupport = false;
                capstoneSupport = false;
                alsaSupport = false;
                guestAgentSupport = false;
                numaSupport = false;
                seccompSupport = false;
                vncSupport = false;
                usbredirSupport = false;
                openGLSupport = false;
                virglSupport = false;
                rutabagaSupport = false;
                uringSupport = false;
              })
            ];
          };
        }
      );

      packages = forAllSystems (system:
        let pkgs = nixpkgs.legacyPackages.${system};
        in {
          test-riscv = pkgs.pkgsCross.riscv64.stdenv.mkDerivation {
            pname = "test-riscv";
            version = "1.0";

            src = ./.;

            buildPhase = ''
              $CC test.c -o test
            '';

            installPhase = ''
              mkdir -p $out/bin
              cp test $out/bin/
            '';
          };
        }
      );
    };
}
