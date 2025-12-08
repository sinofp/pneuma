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

          # Create a clang with --target wrapper for U-Boot
          clangRiscv = pkgs.writeShellScriptBin "clang-riscv64" ''
            exec ${pkgs.llvmPackages_latest.clang-unwrapped}/bin/clang --target=riscv64-unknown-elf "$@"
          '';

          inherit (pkgs.callPackage "${nixpkgs}/pkgs/misc/uboot" { }) buildUBoot;

          uboot = (
            defconfig:
            buildUBoot {
              inherit defconfig;
              extraMeta.platforms = [ system ];
              OPENSBI = "${opensbi}/share/opensbi/lp64/generic/firmware/fw_dynamic.bin";
              filesToInstall = [ "u-boot.bin" ];
              extraMakeFlags = [
                "LLVM=1"
                "LLVM_IAS=1"
                "CC=${clangRiscv}/bin/clang-riscv64"
                "LD=${pkgs.llvmPackages_latest.lld}/bin/ld.lld"
                "OBJCOPY=${pkgs.llvmPackages_latest.bintools-unwrapped}/bin/llvm-objcopy"
                "AR=${pkgs.llvmPackages_latest.bintools-unwrapped}/bin/llvm-ar"
              ];
              # Disable EFI loader and self-tests which have host architecture detection issues
              extraConfig = ''
                # CONFIG_EFI_LOADER is not set
                # CONFIG_CMD_BOOTEFI is not set
              '';
            }
          );

          # TODO starfive_visionfive2_defconfig
          ubootQemu = uboot "qemu-riscv64_smode_defconfig";

        in
        {
          default = pkgs.mkShell {
            buildInputs = with pkgs; [
              # Unwrapped LLVM/clang for bare-metal RISC-V development
              llvmPackages_latest.clang-unwrapped
              llvmPackages_latest.bintools-unwrapped # includes lld, llvm-nm, llvm-objdump, etc.
              opensbi
              ubootQemu
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
              export UBOOT_QEMU="${ubootQemu}/u-boot.bin"
              export CLANG="${pkgs.llvmPackages_latest.clang-unwrapped}/bin/clang"
              export LLD="${pkgs.llvmPackages_latest.lld}/bin/ld.lld"
              export PATH="$PWD:$PATH" # run, fmt
              echo "RISC-V bare-metal development environment loaded"
              echo "Clang: $CLANG"
              echo "LLD: $LLD"
              echo "OpenSBI firmware: $QEMU_OPENSBI"
              echo "U-Boot (QEMU): $UBOOT_QEMU"
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

          # Helper function to build kernel with embedded shell for a specific architecture
          mkKernel =
            arch:
            let
              cfg = archConfigs.${arch};
              # Use -fPIE to avoid linker error on RV64. -mcmodel=medany doesn't help.
              pie = if arch == "riscv64" then "-fPIE" else "";
              ccflags = "-std=c++2c -O2 -g3 -Wall -Wextra --target=${cfg.target} -fno-stack-protector -ffreestanding -nostdlib ${pie}";
            in
            pkgs.runCommand "build-kernel-${arch}"
              {
                nativeBuildInputs = with pkgs.llvmPackages_latest; [
                  clang-unwrapped
                  bintools-unwrapped
                ];
              }
              ''
                                mkdir -p $out

                                # Copy all source files
                                cp ${./shell.cc} shell.cc
                                cp ${./user.cc} user.cc
                                cp ${./user.h} user.h
                                cp ${./kernel.cc} kernel.cc
                                cp ${./kernel.h} kernel.h
                                cp ${./common.cc} common.cc
                                cp ${./common.h} common.h
                                cp ${./span.h} span.h
                                cp ${./virtio.h} virtio.h
                                cp ${./file.h} file.h
                                cp ${./user.ld} user.ld
                                cp ${./kernel.ld} kernel.ld

                                # Build shell.elf (for debugging symbols)
                                ${clang} ${ccflags} \
                                  --ld-path=${lld} \
                                  -Wl,-Tuser.ld \
                                  -Wl,-Map=$out/shell.map \
                                  -o $out/shell.elf \
                                  user.cc common.cc shell.cc

                                # Convert shell.elf to binary
                                ${objcopy} --set-section-flags .bss=alloc,contents -O binary \
                                  $out/shell.elf $out/shell.bin

                                # Convert binary to ELF object with clean symbol names
                                # cd into $out so objcopy uses just "shell.bin" as the filename for generated symbols
                                pushd $out
                		${objcopy} -I binary -O ${cfg.elfFormat} shell.bin shell.bin.o
                		popd

                                # Build kernel.elf with embedded shell
                                ${clang} ${ccflags} \
                                  --ld-path=${lld} \
                                  -Wl,-Tkernel.ld \
                                  -Wl,-Map=$out/kernel.map \
                                  -o $out/kernel.elf \
                                  kernel.cc common.cc $out/shell.bin.o
              '';

          # Build both architectures
          build32 = mkKernel "riscv32";
          build64 = mkKernel "riscv64";

        in
        {
          rv32 = build32;
          rv64 = build64;
          default = build32;
        }
      );
    };
}
