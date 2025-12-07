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

      # Architecture configurations
      archConfigs = {
        riscv32 = {
          target = "riscv32-unknown-elf";
          elfFormat = "elf32-littleriscv";
        };
        riscv64 = {
          target = "riscv64-unknown-elf";
          elfFormat = "elf64-littleriscv";
        };
      };

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
	      export PATH="$PWD:$PATH" # run
              echo "RISC-V bare-metal development environment loaded"
              echo "Clang: $CLANG"
              echo "LLD: $LLD"
              echo "OpenSBI firmware: $QEMU_OPENSBI"
              echo ""
              echo "Build targets:"
              echo "  nix build .#rv32        # RV32 kernel"
              echo "  nix build .#rv32-shell  # RV32 shell"
              echo "  nix build .#rv64        # RV64 kernel"
              echo "  nix build .#rv64-shell  # RV64 shell"
              echo ""
              echo "Run with:"
              echo "  run           # RV32 (default)"
              echo "  run rv64      # RV64"
              echo "  run --debug   # RV32 with debugger"
              echo "  run rv64 --debug  # RV64 with debugger"
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

          # Helper function to build for a specific architecture
          mkKernel =
            arch:
            let
              cfg = archConfigs.${arch};
              cflags = "-std=c11 -O2 -g3 -Wall -Wextra --target=${cfg.target} -fno-stack-protector -ffreestanding -nostdlib";
              ccflags = "-std=c++2c -O2 -g3 -Wall -Wextra --target=${cfg.target} -fno-stack-protector -ffreestanding -nostdlib";

              # Build the shell application
              shell =
                pkgs.runCommand "build-shell-${arch}"
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
                    ${objcopy} -I binary -O ${cfg.elfFormat} \
                    shell.bin shell.bin.o
                  '';

              # Build the kernel
              kernel =
                pkgs.runCommand "build-kernel-${arch}"
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

            in
            {
              inherit shell kernel;
              default = kernel;
            };

          # Build both architectures
          build32 = mkKernel "riscv32";
          build64 = mkKernel "riscv64";

        in
        {
          # RV32 packages (short names)
          rv32 = build32.kernel;
          rv32-shell = build32.shell;

          # RV64 packages (short names)
          rv64 = build64.kernel;
          rv64-shell = build64.shell;

          # Default to RV32
          default = build32.kernel;

          # Legacy aliases for compatibility
          shell = build32.shell;
          riscv32 = build32.kernel;
          riscv64 = build64.kernel;
        }
      );
    };
}
