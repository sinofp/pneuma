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

      # Below are all shared between devShells and packages

      toolchainFor = pkgs: {
        clang = pkgs.llvmPackages_latest.clang-unwrapped;
        lld = pkgs.llvmPackages_latest.lld;
        bintools = pkgs.llvmPackages_latest.bintools-unwrapped; # llvm-nm, llvm-objdump, llvm-objcopy, llvm-ar, etc.
      };

      nativeBuildInputsFor =
        pkgs: with pkgs; [
          cmake
          ninja
          dosfstools # mkfs.vfat
          mtools # mcopy
          zsh # scripts/*
          (toolchainFor pkgs).bintools # clang and lld will be included in commonEnvFor to avoid Apple's variant
        ];

      commonEnvFor =
        pkgs:
        let
          t = toolchainFor pkgs;
        in
        {
          CLANG = "${t.clang}/bin/clang";
          LLD = "${t.lld}/bin/ld.lld";
          FATFS_SRC = "${fatfsSrcFor pkgs}";
          DOOMGENERIC_SRC = "${doomgenericSrcFor pkgs}";
        };

      fatfsSrcFor =
        pkgs:
        pkgs.fetchzip {
          url = "https://elm-chan.org/fsw/ff/arc/ff16.zip";
          hash = "sha256-FyN+We8M68pxtltRrRgc8L8bpWhsnQMYfRLxIQNE1Bg=";
          stripRoot = false;
        };

      doomgenericSrcFor =
        pkgs:
        pkgs.fetchFromGitHub {
          owner = "ozkl";
          repo = "doomgeneric";
          rev = "fc601639494e089702a1ada082eb51aaafc03722";
          hash = "sha256-JGa7gTvSvxlmCLw9E+RDMt5RRjorNuA0GnC1kU9xnZ0=";
        };

      doom1WadFor =
        pkgs:
        pkgs.fetchurl {
          url = "https://dav.emptystack.top/public/DOOM1.WAD";
          hash = "sha256-HX1DvlAeZ9kn5BXguPPinDvzMHXoWXIYFvZSpSbKx3E=";
        };

    in
    {
      devShells = forAllSystems (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          t = toolchainFor pkgs;

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
            exec ${t.clang}/bin/clang --target=riscv64-unknown-elf "$@"
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
                "LD=${t.lld}/bin/ld.lld"
                "OBJCOPY=${t.bintools}/bin/llvm-objcopy"
                "AR=${t.bintools}/bin/llvm-ar"
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

          customQemu =
            (pkgs.qemu.override {
              hostCpuTargets = [
                "riscv64-softmmu"
                "riscv32-softmmu"
              ];
              minimal = true;
              gtkSupport = !pkgs.stdenv.hostPlatform.isDarwin;
              uringSupport = false;
            }).overrideAttrs
              (old: {
                patches = (old.patches or [ ]) ++ [ ./patches/qemu-jh7110.patch ];
              });

          nativeBuildInputs = nativeBuildInputsFor pkgs;

          commonEnv = commonEnvFor pkgs;

          toolchain = toolchainFor pkgs;
        in
        {
          default = pkgs.mkShell {
            buildInputs =
              (with pkgs; [
                gdb-dashboard
                opensbi
                ubootQemu
                customQemu
                dtc
                picocom
                lrzsz # sb
                android-tools # fastboot
                expect # Missing on RPi
              ])
              ++ nativeBuildInputs;

            shellHook = ''
              ${pkgs.lib.concatStrings (
                pkgs.lib.mapAttrsToList (name: value: "export ${name}=\"${value}\"\n") commonEnv
              )}
              export QEMU_OPENSBI="${opensbi}/share/opensbi"
              export UBOOT_QEMU="${ubootQemu}/u-boot.bin"
              export PATH="${toolchain.clang}/bin:${ubootQemu}:$PWD/scripts:$PATH" # clang-format/clangd, mkimage (from U-Boot), run, fmt
              export DOOM_WAD="${doom1WadFor pkgs}"
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

          mkPneuma =
            arch:
            pkgs.stdenv.mkDerivation {
              pname = "pneuma-${arch}";
              version = "unstable";
              src = self;

              nativeBuildInputs = nativeBuildInputsFor pkgs;

              postPatch = ''
                patchShebangs scripts
              '';

              cmakeFlags = [
                "-GNinja"
                "-DCMAKE_TOOLCHAIN_FILE=${self}/cmake/toolchain-${arch}.cmake"
                # Nixpkgs' cmake setup hook defaults to Release (-O3 -DNDEBUG)
                # when CMAKE_BUILD_TYPE is unset.
                # -O3 constant-folded the all-zero PCB table from .bss into 16 MB of
                # literal .data instead of leaving it as a zero-fill reservation.
                "-DCMAKE_BUILD_TYPE="
              ];

              env = commonEnvFor pkgs;

              enableParallelBuilding = true;

              installPhase = ''
                mkdir -p $out
                cp *.elf Image disk.img *.map compile_commands.json $out/
              '';
            };

          build32 = mkPneuma "riscv32";
          build64 = mkPneuma "riscv64";

        in
        {
          rv32 = build32;
          rv64 = build64;
          default = build64;
          compile-commands = pkgs.runCommand "compile_commands.json" { } ''
            cp ${build64}/compile_commands.json $out
          '';
        }
      );
    };
}
