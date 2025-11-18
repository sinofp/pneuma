{
  description = "RISC-V OS develop environment";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" "aarch64-darwin" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f system);
    in {
      devShells = forAllSystems (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};

          # Fetch pre-built OpenSBI firmware binaries
          opensbi-fw = pkgs.fetchurl {
            url = "https://github.com/riscv-software-src/opensbi/releases/download/v1.7/opensbi-1.7-rv-bin.tar.xz";
            hash = "sha256-l6b1MnZJUgPCXbjOD3B5Nq+W8L+g312p5QxCbBJmcWQ=";
          };

          # Extract firmware binaries
          opensbi = pkgs.runCommand "unpack OpenSBI archive" {} ''
            mkdir -p $out/share/opensbi
            tar xf ${opensbi-fw} -C $out/share/opensbi --strip-components=1
          '';

        in {
          default = pkgs.mkShell {
            buildInputs = with pkgs; [
              # Unwrapped LLVM/clang for bare-metal RISC-V development
              llvmPackages_latest.clang-unwrapped
              llvmPackages_latest.lld
              # OpenSBI firmware for RISC-V (pre-built)
              opensbi
              (qemu.override {
                hostCpuTargets = [ "riscv64-softmmu" "riscv32-softmmu" ];
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

            shellHook = ''
              export QEMU_OPENSBI="${opensbi}/share/opensbi"
              export CLANG="${pkgs.llvmPackages_latest.clang-unwrapped}/bin/clang"
              export LLD="${pkgs.llvmPackages_latest.lld}/bin/ld.lld"
              echo "RISC-V bare-metal development environment loaded"
              echo "Clang: $CLANG"
              echo "LLD: $LLD"
              echo "OpenSBI firmware: $QEMU_OPENSBI"
            '';
          };
        }
      );

      packages = forAllSystems (system:
        let pkgs = nixpkgs.legacyPackages.${system};
        in {
          default = pkgs.runCommand "build kernel" {
            nativeBuildInputs = with pkgs.llvmPackages_latest; [ clang-unwrapped lld ];
          } ''
            mkdir -p $out
            ${pkgs.llvmPackages_latest.clang-unwrapped}/bin/clang \
              -std=c11 -O2 -g3 -Wall -Wextra \
              --target=riscv32-unknown-elf \
              -fno-stack-protector -ffreestanding -nostdlib \
              --ld-path=${pkgs.llvmPackages_latest.lld}/bin/ld.lld \
              -Wl,-T${./kernel.ld} \
              -Wl,-Map=$out/kernel.map \
              -o $out/kernel.elf \
              ${./kernel.c}
          '';
        }
      );
    };
}
