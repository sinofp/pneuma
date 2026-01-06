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
              gdb-dashboard
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
              dtc
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

          # Source file lists (using Nix path syntax)
          kernelSources = [
            ./kernel.cc
            ./proc.cc
            ./mem.cc
            ./sbi.cc
            ./common.cc
            ./virtio-blk.cc
            ./ustar.cc
            ./trap.cc
            ./qtab.cc
            ./fdt.cc
          ];
          shellSources = [
            ./user.cc
            ./common.cc
            ./shell.cc
          ];
          userASources = [
            ./user.cc
            ./common.cc
            ./user-a.cc
          ];
          userBSources = [
            ./user.cc
            ./common.cc
            ./user-b.cc
          ];
          headers = [
            ./kernel.h
            ./common.h
            ./span.h
            ./virtio.h
            ./file.h
            ./mem.h
            ./sbi.h
            ./proc.h
            ./user.h
            ./trap.h
            ./csr.h
            ./plic.h
            ./qtab.h
            ./optional.h
            ./debug.h
            ./fdt.h
            ./pagetable.h
          ];
          linkerScripts = [
            ./user.ld
            ./kernel.ld
          ];

          # All source files with duplicates removed
          allSourcePaths = pkgs.lib.unique (
            kernelSources ++ shellSources ++ userASources ++ userBSources ++ headers ++ linkerScripts
          );

          # Helper to get just the filename from a path
          basename = path: builtins.baseNameOf (toString path);

          # Helper function to build kernel with embedded shell for a specific architecture
          mkKernel =
            arch:
            let
              cfg = archConfigs.${arch};
              # Use -fPIE to avoid linker error on RV64. -mcmodel=medany doesn't help.
              pie = if arch == "riscv64" then "-fPIE" else "";
              ccflags = "-std=c++2c -g3 -Wall -Wextra --target=${cfg.target} -fno-stack-protector -ffreestanding -nostdlib -fno-exceptions ${pie}";
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

                # Copy all source files automatically using Nix paths
                ${pkgs.lib.concatMapStringsSep "\n" (path: "cp ${path} ${basename path}") allSourcePaths}

                # Build shell.elf (for debugging symbols)
                ${clang} ${ccflags} \
                  --ld-path=${lld} \
                  -Wl,-Tuser.ld \
                  -Wl,-Map=$out/shell.map \
                  -o $out/shell.elf \
                  ${pkgs.lib.concatStringsSep " " (map basename shellSources)}

                # Convert shell.elf to binary
                ${objcopy} --set-section-flags .bss=alloc,contents -O binary \
                  $out/shell.elf $out/shell.bin

                # Convert binary to ELF object with clean symbol names
                pushd $out
                ${objcopy} -I binary -O ${cfg.elfFormat} shell.bin shell.bin.o
                popd

                # Build user-a.elf (for debugging symbols)
                ${clang} ${ccflags} \
                  --ld-path=${lld} \
                  -Wl,-Tuser.ld \
                  -Wl,-Map=$out/user-a.map \
                  -o $out/user-a.elf \
                  ${pkgs.lib.concatStringsSep " " (map basename userASources)}

                # Convert user-a.elf to binary
                ${objcopy} --set-section-flags .bss=alloc,contents -O binary \
                  $out/user-a.elf $out/user-a.bin

                # Convert binary to ELF object
                pushd $out
                ${objcopy} -I binary -O ${cfg.elfFormat} user-a.bin user-a.bin.o
                popd

                # Build user-b.elf (for debugging symbols)
                ${clang} ${ccflags} \
                  --ld-path=${lld} \
                  -Wl,-Tuser.ld \
                  -Wl,-Map=$out/user-b.map \
                  -o $out/user-b.elf \
                  ${pkgs.lib.concatStringsSep " " (map basename userBSources)}

                # Convert user-b.elf to binary
                ${objcopy} --set-section-flags .bss=alloc,contents -O binary \
                  $out/user-b.elf $out/user-b.bin

                # Convert binary to ELF object
                pushd $out
                ${objcopy} -I binary -O ${cfg.elfFormat} user-b.bin user-b.bin.o
                popd

                # Build kernel.elf with embedded user programs
                ${clang} ${ccflags} \
                  --ld-path=${lld} \
                  -Wl,-Tkernel.ld \
                  -Wl,-Map=$out/kernel.map \
                  -o $out/kernel.elf \
                  ${pkgs.lib.concatStringsSep " " (map basename kernelSources)} \
                  $out/shell.bin.o $out/user-a.bin.o $out/user-b.bin.o
              '';

          # Helper function to generate compile_commands.json for clangd
          mkCompileCommands =
            arch:
            let
              cfg = archConfigs.${arch};
              pie = if arch == "riscv64" then "-fPIE" else "";
              ccflags = "-std=c++2c -O2 -g3 -Wall -Wextra --target=${cfg.target} -fno-stack-protector -ffreestanding -nostdlib ${pie}";

              # All unique source files (just basenames)
              allSources = pkgs.lib.unique (kernelSources ++ shellSources ++ userASources ++ userBSources);

              # Generate a compile command entry for each source file
              # Use current directory (.) which clangd will resolve relative to compile_commands.json location
              mkEntry = src: {
                directory = ".";
                command = "${clang} ${ccflags} -c ${basename src}";
                file = "${basename src}";
              };

              entries = map mkEntry allSources;
              jsonContent = builtins.toJSON entries;

            in
            pkgs.writeTextFile {
              name = "compile_commands.json";
              text = jsonContent;
            };

          # Build both architectures
          build32 = mkKernel "riscv32";
          build64 = mkKernel "riscv64";

        in
        {
          rv32 = build32;
          rv64 = build64;
          default = build32;

          compile-commands = mkCompileCommands "riscv64";
        }
      );
    };
}
