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
          lib = pkgs.lib;
          clang = "${pkgs.llvmPackages_latest.clang-unwrapped}/bin/clang";
          objcopy = "${pkgs.llvmPackages_latest.bintools-unwrapped}/bin/llvm-objcopy";
          lld = "${pkgs.llvmPackages_latest.lld}/bin/ld.lld";

          # Fetch FatFs source
          fatfs-src = pkgs.fetchzip {
            url = "https://elm-chan.org/fsw/ff/arc/ff16.zip";
            hash = "sha256-FyN+We8M68pxtltRrRgc8L8bpWhsnQMYfRLxIQNE1Bg=";
            stripRoot = false;
          };

          # Helper to find files recursively
          findFiles =
            dir: suffix:
            let
              contents = builtins.readDir dir;
              processEntry =
                name: type:
                let
                  path = dir + "/${name}";
                in
                if type == "directory" then
                  findFiles path suffix
                else if lib.hasSuffix suffix name then
                  [ path ]
                else
                  [ ];
            in
            builtins.concatLists (lib.mapAttrsToList processEntry contents);

          # Glob all kernel sources and headers
          kernelSources = findFiles ./kernel ".cc";
          kernelHeaders = findFiles ./kernel ".h";

          # Source file lists for libc and user programs
          libcCxxSources = findFiles ./libc/src ".cc";
          libcCSources = findFiles ./libc/src ".c";
          libcHeaders = findFiles ./libc/include ".h";
          userPrograms = (findFiles ./user ".cc") ++ (findFiles ./user ".c");

          # Helper to get just the filename from a path
          basename = path: builtins.baseNameOf (toString path);

          # Get relative path within kernel directory
          kernelRelPath =
            path:
            let
              pathStr = toString path;
              kernelDir = toString ./kernel + "/";
            in
            lib.removePrefix kernelDir pathStr;

          # Get relative path within libc/include directory
          libcIncludeRelPath =
            path:
            let
              pathStr = toString path;
              libcIncludeDir = toString ./libc/include + "/";
            in
            lib.removePrefix libcIncludeDir pathStr;

          # Get relative path within libc/src directory
          libcSrcRelPath =
            path:
            let
              pathStr = toString path;
              libcSrcDir = toString ./libc/src + "/";
            in
            lib.removePrefix libcSrcDir pathStr;

          # Compiler flags for each architecture
          mkFlags =
            arch: includeDir:
            let
              cfg = archConfigs.${arch};
              pie = if arch == "riscv64" then "-fPIE" else "";
              common = "-g3 -Wall -Wextra --target=${cfg.target} -fno-stack-protector -ffreestanding -nostdlib ${pie}";
            in
            {
              # Kernel C++ flags
              kernel = "-std=c++2c ${common} -fno-exceptions -I kernel";
              # User C++ flags
              cxx = "-std=c++2c ${common} -fno-exceptions -nostdlibinc -isystem ${includeDir}";
              # User C flags
              c = "-std=c2x ${common} -nostdlibinc -isystem ${includeDir}";
              # FatFs C flags (relaxed warnings for third-party code)
              fatfs = "-std=c2x -g3 --target=${cfg.target} -fno-stack-protector -ffreestanding -nostdlib ${pie} -nostdlibinc -isystem ${includeDir} -Wno-unused-parameter";
            };

          # Build libc + FatFs for a specific architecture
          mkLibc =
            arch:
            let
              flags = mkFlags arch "include";
            in
            pkgs.runCommand "libc-${arch}"
              {
                nativeBuildInputs = [ pkgs.llvmPackages_latest.clang-unwrapped ];
              }
              ''
                mkdir -p $out

                # Copy libc headers
                mkdir -p include
                ${lib.concatMapStringsSep "\n" (
                  path:
                  let
                    relPath = libcIncludeRelPath path;
                    dir = builtins.dirOf relPath;
                  in
                  ''
                    mkdir -p include/${dir}
                    cp ${path} include/${relPath}
                  ''
                ) libcHeaders}

                # Copy libc sources
                mkdir -p src
                ${lib.concatMapStringsSep "\n" (
                  path:
                  let
                    relPath = libcSrcRelPath path;
                    dir = builtins.dirOf relPath;
                  in
                  ''
                    mkdir -p src/${dir}
                    cp ${path} src/${relPath}
                  ''
                ) (libcCxxSources ++ libcCSources)}

                # Copy FatFs source files from upstream
                mkdir -p fatfs
                cp ${fatfs-src}/source/ff.c fatfs/
                cp ${fatfs-src}/source/ff.h fatfs/
                cp ${fatfs-src}/source/diskio.h fatfs/
                cp ${fatfs-src}/source/ffconf.h fatfs/ffconf.h
                # Patch ffconf.h to switch from Japnese to U.S.
                sed -i 's|^#define FF_CODE_PAGE.*|#define FF_CODE_PAGE 437|' fatfs/ffconf.h
                # I don't want to implement get_fattime()
                sed -i 's|^#define FF_FS_NORTC.*|#define FF_FS_NORTC 1|' fatfs/ffconf.h
                # Copy our custom diskio implementation
                cp ${./libc/fatfs/diskio.c} fatfs/diskio.c

                # Compile FatFs
                ${clang} ${flags.fatfs} -I fatfs -c -o $out/fatfs-ff.o fatfs/ff.c
                ${clang} ${flags.fatfs} -I fatfs -c -o $out/fatfs-diskio.o fatfs/diskio.c

                # Compile libc C++ sources
                ${lib.concatMapStringsSep "\n" (
                  src:
                  let
                    relPath = libcSrcRelPath src;
                    name = lib.removeSuffix ".cc" (builtins.replaceStrings [ "/" ] [ "-" ] relPath);
                  in
                  ''
                    ${clang} ${flags.cxx} -I fatfs -c -o $out/libc-${name}.o src/${relPath}
                  ''
                ) libcCxxSources}

                # Compile libc C sources
                ${lib.concatMapStringsSep "\n" (
                  src:
                  let
                    relPath = libcSrcRelPath src;
                    name = lib.removeSuffix ".c" (builtins.replaceStrings [ "/" ] [ "-" ] relPath);
                  in
                  ''
                    ${clang} ${flags.c} -I fatfs -c -o $out/libc-${name}.o src/${relPath}
                  ''
                ) libcCSources}

                # Also copy headers and linker script for user programs
                cp -r include $out/include
                cp ${./user/user.ld} $out/user.ld
              '';

          # Build kernel with embedded user programs
          mkKernel =
            arch:
            let
              cfg = archConfigs.${arch};
              libc = mkLibc arch;
              flags = mkFlags arch "${libc}/include";

              # List all libc object files
              libcCxxObjs = map (
                p:
                "${libc}/libc-${
                  lib.removeSuffix ".cc" (builtins.replaceStrings [ "/" ] [ "-" ] (libcSrcRelPath p))
                }.o"
              ) libcCxxSources;
              libcCObjs = map (
                p:
                "${libc}/libc-${
                  lib.removeSuffix ".c" (builtins.replaceStrings [ "/" ] [ "-" ] (libcSrcRelPath p))
                }.o"
              ) libcCSources;
              fatfsObjs = [
                "${libc}/fatfs-ff.o"
                "${libc}/fatfs-diskio.o"
              ];
              allLibcObjs = libcCxxObjs ++ libcCObjs ++ fatfsObjs;
            in
            pkgs.runCommand "build-kernel-${arch}"
              {
                nativeBuildInputs = with pkgs.llvmPackages_latest; [
                  clang-unwrapped
                  bintools-unwrapped
                  pkgs.dosfstools
                  pkgs.mtools
                ];
              }
              ''
                mkdir -p $out

                # Copy kernel directory structure
                cp -r ${./kernel} kernel
                chmod -R u+w kernel

                # Copy user directory
                mkdir -p user
                ${lib.concatMapStringsSep "\n" (path: "cp ${path} user/${basename path}") userPrograms}

                # Build user programs
                ${lib.concatMapStringsSep "\n" (
                  prog:
                  let
                    progFile = basename prog;
                    name = lib.removeSuffix ".c" (lib.removeSuffix ".cc" progFile);
                    isC = lib.hasSuffix ".c" progFile;
                    progFlags = if isC then flags.c else flags.cxx;
                  in
                  ''
                    # Build ${name}.elf
                    ${clang} ${progFlags} -c -o user-${name}.o user/${progFile}
                    ${clang} ${flags.cxx} \
                      --ld-path=${lld} \
                      -Wl,-T${libc}/user.ld \
                      -Wl,-Map=$out/${name}.map \
                      -o $out/${name}.elf \
                      ${lib.concatStringsSep " " allLibcObjs} user-${name}.o

                    # Convert to binary and ELF object
                    ${objcopy} --set-section-flags .bss=alloc,contents -O binary $out/${name}.elf $out/${name}.bin
                    pushd $out
                    ${objcopy} -I binary -O ${cfg.elfFormat} ${name}.bin ${name}.bin.o
                    popd
                  ''
                ) userPrograms}

                # Build kernel.elf with embedded user programs
                ${clang} ${flags.kernel} \
                  --ld-path=${lld} \
                  -Wl,-Tkernel/kernel.ld \
                  -Wl,-Map=$out/kernel.map \
                  -o $out/kernel.elf \
                  ${lib.concatStringsSep " " (map (p: "kernel/${kernelRelPath p}") kernelSources)} \
                  ${lib.concatStringsSep " " (
                    map (
                      prog: "$out/${lib.removeSuffix ".c" (lib.removeSuffix ".cc" (basename prog))}.bin.o"
                    ) userPrograms
                  )}

                # Generate Image file for U-Boot booti
                ${objcopy} -O binary $out/kernel.elf $out/Image

                # Create FAT disk image (32MB FAT16, must be bigger than kernel image to be selected as disk in virtio-blk.cc)
                dd if=/dev/zero of=$out/disk.img bs=1M count=32
                mkfs.fat -F 16 $out/disk.img
                # Copy files from disk/ directory if it exists
                for f in ${./disk}/*; do
                  if [ -f "$f" ]; then
                    mcopy -i $out/disk.img "$f" "::$(basename "$f")"
                  fi
                done
              '';

          # Helper function to generate compile_commands.json for clangd
          mkCompileCommands =
            arch:
            let
              flags = mkFlags arch "libc/include";

              # Generate entries for kernel sources
              kernelEntries = map (src: {
                directory = ".";
                command = "${clang} ${flags.kernel} -c kernel/${kernelRelPath src}";
                file = "kernel/${kernelRelPath src}";
              }) kernelSources;

              # Generate entries for libc C++ sources
              libcCxxEntries = map (src: {
                directory = ".";
                command = "${clang} ${flags.cxx} -I libc/fatfs -c libc/src/${libcSrcRelPath src}";
                file = "libc/src/${libcSrcRelPath src}";
              }) libcCxxSources;

              # Generate entries for libc C sources
              libcCEntries = map (src: {
                directory = ".";
                command = "${clang} ${flags.c} -I libc/fatfs -c libc/src/${libcSrcRelPath src}";
                file = "libc/src/${libcSrcRelPath src}";
              }) libcCSources;

              # Generate entries for user programs
              userEntries = map (
                src:
                let
                  file = basename src;
                  isC = lib.hasSuffix ".c" file;
                  progFlags = if isC then flags.c else flags.cxx;
                in
                {
                  directory = ".";
                  command = "${clang} ${progFlags} -c user/${file}";
                  file = "user/${file}";
                }
              ) userPrograms;

              entries = kernelEntries ++ libcCxxEntries ++ libcCEntries ++ userEntries;
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
          default = build64;
          compile-commands = mkCompileCommands "riscv64";
        }
      );
    };
}
