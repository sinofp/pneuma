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
              filesToInstall = [
                "u-boot.bin"
                "tools/mkimage"
              ];
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
              dosfstools # mkfs.vfat
              mtools # mcopy
            ];

            shellHook = ''
              export QEMU_OPENSBI="${opensbi}/share/opensbi"
              export UBOOT_QEMU="${ubootQemu}/u-boot.bin"
              export CLANG="${pkgs.llvmPackages_latest.clang-unwrapped}/bin/clang"
              export LLD="${pkgs.llvmPackages_latest.lld}/bin/ld.lld"
              export PATH="${ubootQemu}:$PWD:$PATH" # mkimage (from U-Boot), run, fmt
              echo "RISC-V bare-metal development environment loaded"
              echo "Clang: $CLANG"
              echo "LLD: $LLD"
              echo "OpenSBI firmware: $QEMU_OPENSBI"
              echo "U-Boot (QEMU): $UBOOT_QEMU"
              echo "mkimage: ${ubootQemu}/mkimage"
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
          libcSources = [
            ./libc/src/crt0.cc
            ./libc/src/syscall.cc
            ./libc/src/stdio.cc
            ./libc/src/stdlib.cc
            ./libc/src/string.cc
            ./libc/src/misc.cc
          ];
          userPrograms = [
            ./user/shell.cc
            ./user/a.cc
            ./user/b.cc
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
            ./trap.h
            ./csr.h
            ./plic.h
            ./qtab.h
            ./optional.h
            ./debug.h
            ./fdt.h
            ./pagetable.h
          ];
          libcHeaders = [
            ./libc/include/sys/syscall.h
            ./libc/include/sys/misc.h
            ./libc/include/stdio.h
            ./libc/include/stdlib.h
            ./libc/include/string.h
            ./libc/include/stdarg.h
          ];
          linkerScripts = [
            ./user/user.ld
            ./kernel.ld
          ];

          # All source files with duplicates removed (flat files only)
          flatSourcePaths = pkgs.lib.unique (
            kernelSources
            ++ headers
            ++ [./kernel.ld]
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
              userCcflags = "${ccflags} -nostdlibinc -isystem libc/include";
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

                # Copy flat source files
                ${pkgs.lib.concatMapStringsSep "\n" (path: "cp ${path} ${basename path}") flatSourcePaths}

                # Copy libc directory structure
                mkdir -p libc/include/sys libc/src
                ${pkgs.lib.concatMapStringsSep "\n" (path: "cp ${path} libc/src/${basename path}") libcSources}
                ${pkgs.lib.concatMapStringsSep "\n" (
                  path:
                  let
                    name = basename path;
                  in
                  if name == "syscall.h" || name == "misc.h" then
                    "cp ${path} libc/include/sys/${name}"
                  else
                    "cp ${path} libc/include/${name}"
                ) libcHeaders}

                # Copy user directory
                mkdir -p user
                ${pkgs.lib.concatMapStringsSep "\n" (path: "cp ${path} user/${basename path}") userPrograms}
                cp ${./user/user.ld} user/user.ld

                # Build user programs
                ${pkgs.lib.concatMapStringsSep "\n" (prog:
                  let
                    name = pkgs.lib.removeSuffix ".cc" (basename prog);
                  in ''
                # Build ${name}.elf
                ${clang} ${userCcflags} \
                  --ld-path=${lld} \
                  -Wl,-Tuser/user.ld \
                  -Wl,-Map=$out/${name}.map \
                  -o $out/${name}.elf \
                  ${pkgs.lib.concatStringsSep " " (map (p: "libc/src/${basename p}") libcSources)} user/${basename prog}

                # Convert to binary and ELF object
                ${objcopy} --set-section-flags .bss=alloc,contents -O binary $out/${name}.elf $out/${name}.bin
                pushd $out
                ${objcopy} -I binary -O ${cfg.elfFormat} ${name}.bin ${name}.bin.o
                popd
                ''
                ) userPrograms}

                # Build kernel.elf with embedded user programs
                ${clang} ${ccflags} \
                  --ld-path=${lld} \
                  -Wl,-Tkernel.ld \
                  -Wl,-Map=$out/kernel.map \
                  -o $out/kernel.elf \
                  ${pkgs.lib.concatStringsSep " " (map basename kernelSources)} \
                  ${pkgs.lib.concatStringsSep " " (map (prog: "$out/${pkgs.lib.removeSuffix ".cc" (basename prog)}.bin.o") userPrograms)}

                # Generate Image file for U-Boot booti
                ${objcopy} -O binary $out/kernel.elf $out/Image
              '';

          # Helper function to generate compile_commands.json for clangd
          mkCompileCommands =
            arch:
            let
              cfg = archConfigs.${arch};
              pie = if arch == "riscv64" then "-fPIE" else "";
              ccflags = "-std=c++2c -O2 -g3 -Wall -Wextra --target=${cfg.target} -fno-stack-protector -ffreestanding -nostdlib ${pie}";
              userCcflags = "${ccflags} -nostdlibinc -isystem libc/include";

              # Generate entries for kernel sources
              kernelEntries = map (src: {
                directory = ".";
                command = "${clang} ${ccflags} -c ${basename src}";
                file = "${basename src}";
              }) kernelSources;

              # Generate entries for libc sources (need -isystem libc/include)
              libcEntries = map (src: {
                directory = ".";
                command = "${clang} ${userCcflags} -c libc/src/${basename src}";
                file = "libc/src/${basename src}";
              }) libcSources;

              # Generate entries for user programs (need -isystem libc/include)
              userEntries = map (src: {
                directory = ".";
                command = "${clang} ${userCcflags} -c user/${basename src}";
                file = "user/${basename src}";
              }) userPrograms;

              entries = kernelEntries ++ libcEntries ++ userEntries;
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
