{
  description = "RISC-V OS develop environment";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs =
    { self, nixpkgs }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
        "aarch64-darwin"
      ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f system);
    in
    {
      devShells = forAllSystems (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};

          # Fetch pre-built OpenSBI firmware binaries
          opensbi-fw = pkgs.fetchurl {
            url = "https://github.com/riscv-software-src/opensbi/releases/download/v1.7/opensbi-1.7-rv-bin.tar.xz";
            hash = "sha256-l6b1MnZJUgPCXbjOD3B5Nq+W8L+g312p5QxCbBJmcWQ=";
          };

          # Extract firmware binaries
          opensbi = pkgs.runCommand "unpack OpenSBI archive" { } ''
            mkdir -p $out/share/opensbi
            tar xf ${opensbi-fw} -C $out/share/opensbi --strip-components=1
          '';

        in
        {
          default = pkgs.mkShell {
            buildInputs = with pkgs; [
              # Unwrapped LLVM/clang for bare-metal RISC-V development
              llvmPackages_latest.clang-unwrapped
              llvmPackages_latest.bintools-unwrapped # includes lld, llvm-nm, llvm-objdump, etc.
              # OpenSBI firmware for RISC-V (pre-built)
              opensbi
              (qemu.override {
                hostCpuTargets = [
                  "riscv64-softmmu"
                  "riscv32-softmmu"
                ];
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

      packages = forAllSystems (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          clang = "${pkgs.llvmPackages_latest.clang-unwrapped}/bin/clang";
          objcopy = "${pkgs.llvmPackages_latest.bintools-unwrapped}/bin/llvm-objcopy";
          lld = "${pkgs.llvmPackages_latest.lld}/bin/ld.lld";
          cflags = "-std=c11 -O2 -g3 -Wall -Wextra --target=riscv32-unknown-elf -fno-stack-protector -ffreestanding -nostdlib";
          ccflags = "-std=c++2c -O2 -g3 -Wall -Wextra --target=riscv32-unknown-elf -fno-stack-protector -ffreestanding -nostdlib";

          # Build the shell application
          shell =
            pkgs.runCommand "build-shell"
              {
                nativeBuildInputs = with pkgs.llvmPackages_latest; [
                  clang-unwrapped
                  bintools-unwrapped
                ];
              }
              ''
                mkdir -p $out
                # Copy source files
                cp ${./shell.cc} shell.cc
                cp ${./user.cc} user.cc
                cp ${./user.h} user.h
                cp ${./common.cc} common.cc
                cp ${./common.h} common.h
                cp ${./user.ld} user.ld

                # Build the shell
                ${clang} ${ccflags} \
                --ld-path=${lld} \
                -Wl,-Tuser.ld \
                -Wl,-Map=$out/shell.map \
                -o $out/shell.elf \
                user.cc common.cc shell.cc

                # Convert to binary
                ${objcopy} --set-section-flags .bss=alloc,contents -O binary \
                $out/shell.elf $out/shell.bin

                # Convert binary to ELF object with clean symbol names
                # cd into $out so objcopy uses just "shell.bin" as the filename for generated symbols
                cd $out
                ${objcopy} -I binary -O elf32-littleriscv \
                shell.bin shell.bin.o
              '';
        in
        {
          # Shell application package
          shell = shell;

          # Kernel package (default)
          default =
            pkgs.runCommand "build-kernel"
              {
                nativeBuildInputs = with pkgs.llvmPackages_latest; [
                  clang-unwrapped
                  bintools-unwrapped
                ];
              }
              ''
                mkdir -p $out
                # Copy source files
                cp ${./kernel.cc} kernel.cc
                cp ${./kernel.h} kernel.h
                cp ${./common.cc} common.cc
                cp ${./common.h} common.h
                cp ${./kernel.ld} kernel.ld

                # Copy the embedded shell object
                cp ${shell}/shell.bin.o shell.bin.o

                # Build kernel.elf with embedded shell
                ${clang} ${ccflags} \
                --ld-path=${lld} \
                -Wl,-Tkernel.ld \
                -Wl,-Map=$out/kernel.map \
                -o $out/kernel.elf \
                kernel.cc common.cc shell.bin.o
              '';
        }
      );
    };
}
