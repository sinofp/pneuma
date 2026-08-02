# Pneuma — RISC-V microkernel + tiny libc + user programs, all linked into one
# kernel.elf. This drives the *compile graph* (fast, header-aware, incremental
# rebuilds via Ninja); the Nix dev shell still owns the toolchain and the
# hermetic third-party fetches (FatFs source, prebuilt doom.bin, OpenSBI, QEMU).
#
# Configure per arch (must be inside `nix develop`):
#   cmake -B build/rv64 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-riscv64.cmake
#   cmake -B build/rv32 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-riscv32.cmake
# Build:      cmake --build build/rv64        (only what changed)
# Artifacts:  build/rv64/{kernel.elf,Image,disk.img,*.elf}

cmake_minimum_required(VERSION 3.21)
project(pneuma C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # free clangd database — replaces mkCompileCommands

# ---------------------------------------------------------------------------
# Host tools and inputs from the Nix dev shell
# ---------------------------------------------------------------------------
find_program(OBJCOPY NAMES llvm-objcopy REQUIRED)
find_program(READELF NAMES llvm-readelf REQUIRED)

# CLANG is already required+set by the toolchain file (cmake/toolchain-*.cmake,
# via cmake/require-env.cmake) before this line ever runs.
include(${CMAKE_SOURCE_DIR}/cmake/require-env.cmake)
require_env(LLD)
require_env(FATFS_SRC)
require_env(DOOMGENERIC_SRC)

set(SCRIPTS ${CMAKE_SOURCE_DIR}/scripts)
set(ARCHFLAGS --target=${PNEUMA_TARGET} -march=${PNEUMA_MARCH} -mabi=${PNEUMA_MABI})

# ---------------------------------------------------------------------------
# Flag sets other than Arch/-fPIE (which are set in toolchain).
# ---------------------------------------------------------------------------
set(COMMON     -g3 -Wall -Wextra
               -fno-stack-protector -fno-omit-frame-pointer -ffreestanding
               -nostdlib -nostdlibinc
               -ffunction-sections -fdata-sections)

set(KERNEL_CXX -std=c++2c ${COMMON} -fno-exceptions -ffixed-x4)
set(USER_CXX   -std=c++2c ${COMMON} -fno-exceptions)
set(USER_C     -std=c2x   ${COMMON})
set(FATFS_C    -std=c2x   ${COMMON} -Wno-unused-parameter)

set(DOOM_C     -std=c99 -g3 ${COMMON}
               -DNORMALUNIX -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200
               -Wno-implicit-function-declaration -Wno-unused-parameter
               -Wno-incompatible-pointer-types -Wno-int-conversion -Wno-sign-compare
               -Wno-pointer-sign)

set(LINKFLAGS  -nostdlib -Wl,--gc-sections)

set(LIBC_INC ${CMAKE_SOURCE_DIR}/libc/include)
set(DRV_INC  ${CMAKE_SOURCE_DIR}/driver)

# rv64: force lld to emit an ET_DYN with R_RISCV_RELATIVE relocations.
if(PNEUMA_PIE)
  set(PIELINK -Wl,-pie -Wl,--no-dynamic-linker -Wl,-z,norelro)
else()
  set(PIELINK "")
endif()

# ---------------------------------------------------------------------------
# Embed helpers: turn a flat binary into a linkable .bin.o carrying
# _binary_<sym>_bin_{start,end,size}, and remember it for the kernel link.
# ---------------------------------------------------------------------------
set_property(GLOBAL PROPERTY PNEUMA_EMBEDS "")

function(_embed_asm sym asmout depend)
  # Assemble a generated .incbin stub into ${sym}.bin.o.
  set(obj ${CMAKE_BINARY_DIR}/embed/${sym}.bin.o)
  add_custom_command(
    OUTPUT ${obj}
    COMMAND ${CLANG} ${ARCHFLAGS} -c ${asmout} -o ${obj}
    DEPENDS ${asmout} ${depend}
    VERBATIM COMMENT "embed ${sym}")
  set_source_files_properties(${obj} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
  set_property(GLOBAL APPEND PROPERTY PNEUMA_EMBEDS ${obj})
endfunction()

# Embed the output of a built user ELF (objcopy elf -> bin -> .bin.o).
# The target name always equals the program name, so one argument suffices.
function(embed_elf name)
  string(REPLACE "-" "_" sym ${name})
  set(bin ${CMAKE_BINARY_DIR}/embed/${name}.bin)
  set(asm ${CMAKE_BINARY_DIR}/embed/${name}.S)
  # --set-section-flags .bss=alloc,contents so image.size() covers code+data+BSS
  # and proc::create can derive heap_start without out-of-band metadata.
  add_custom_command(
    OUTPUT ${bin}
    COMMAND ${OBJCOPY} --set-section-flags .bss=alloc,contents -O binary $<TARGET_FILE:${name}> ${bin}
    DEPENDS ${name}
    VERBATIM COMMENT "objcopy ${name}.elf -> ${name}.bin")
  add_custom_command(
    OUTPUT ${asm}
    COMMAND ${SCRIPTS}/gen-incbin ${sym} ${bin} > ${asm}
    DEPENDS ${bin} ${SCRIPTS}/gen-incbin
    VERBATIM COMMENT "gen ${name}.S")
  _embed_asm(${sym} ${asm} ${bin})
endfunction()

# ---------------------------------------------------------------------------
# FatFs (fs server only). Patch ffconf.h at configure time — no sed step.
# ---------------------------------------------------------------------------
set(FATFS_BD ${CMAKE_BINARY_DIR}/fatfs)
# Stage ff.c/ff.h/diskio.h next to a patched ffconf.h: ff.c's `#include "ff.h"`
# and ff.h's `#include "ffconf.h"` are *quoted*, so they resolve beside the
# source first--compiling ff.c straight from the store would pick up the
# unpatched ffconf.h and pull in get_fattime().
configure_file("${FATFS_SRC}/source/ff.c"      ${FATFS_BD}/ff.c      COPYONLY)
configure_file("${FATFS_SRC}/source/ff.h"      ${FATFS_BD}/ff.h      COPYONLY)
configure_file("${FATFS_SRC}/source/diskio.h"  ${FATFS_BD}/diskio.h  COPYONLY)
file(READ "${FATFS_SRC}/source/ffconf.h" _ffconf)
string(REGEX REPLACE "#define FF_CODE_PAGE[ \t]+[0-9]+" "#define FF_CODE_PAGE 437" _ffconf "${_ffconf}")
string(REGEX REPLACE "#define FF_FS_NORTC[ \t]+[0-9]+"  "#define FF_FS_NORTC 1"    _ffconf "${_ffconf}")
# file(WRITE) is unconditional, but configure_file(COPYONLY) copy-if-different.
# ffconf.h itself (and everything downstream of it) is only touched when the patched content actually changes.
file(WRITE "${FATFS_BD}/ffconf.h.in" "${_ffconf}")
configure_file("${FATFS_BD}/ffconf.h.in" ${FATFS_BD}/ffconf.h COPYONLY)

add_library(fatfs OBJECT
  ${FATFS_BD}/ff.c
  ${CMAKE_SOURCE_DIR}/libc/fatfs/diskio.c)
target_compile_options(fatfs PRIVATE ${FATFS_C})
target_include_directories(fatfs SYSTEM PRIVATE ${FATFS_BD} ${LIBC_INC} ${DRV_INC})

# ---------------------------------------------------------------------------
# libc as an object library linked whole into every user program.
# ---------------------------------------------------------------------------
file(GLOB LIBC_CXX CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/libc/src/*.cc)

add_library(libc OBJECT ${LIBC_CXX})
target_compile_options(libc PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:${USER_CXX}>
  $<$<COMPILE_LANGUAGE:C>:${USER_C}>)
target_include_directories(libc SYSTEM PRIVATE ${LIBC_INC} ${DRV_INC})

# ---------------------------------------------------------------------------
# User programs: user/*.{c,cc} + driver/*.cc (each a standalone process),
# minus doomgeneric_rv.c (part of the doom build).
# ---------------------------------------------------------------------------
file(GLOB USER_C_SRCS   CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/user/*.c)
file(GLOB USER_CXX_SRCS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/user/*.cc)
file(GLOB DRV_CXX_SRCS  CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/driver/*.cc)
list(FILTER USER_C_SRCS EXCLUDE REGEX "doomgeneric_rv")
set(PROGRAMS ${USER_C_SRCS} ${USER_CXX_SRCS} ${DRV_CXX_SRCS})

foreach(src ${PROGRAMS})
  get_filename_component(fname ${src} NAME)
  string(REGEX REPLACE "\\.(c|cc)$" "" name ${fname})

  add_executable(${name} ${src})
  set_target_properties(${name} PROPERTIES
    SUFFIX ".elf"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
  if(fname MATCHES "\\.cc$")
    target_compile_options(${name} PRIVATE ${USER_CXX})
  else()
    target_compile_options(${name} PRIVATE ${USER_C})
  endif()
  target_include_directories(${name} SYSTEM PRIVATE ${LIBC_INC} ${DRV_INC})
  target_link_libraries(${name} PRIVATE libc)

  # fs links FatFs directly instead of talking to itself over IPC.
  # TODO patch FatFs and replace the fs proc. Move diskio.c out of libc/fatfs
  if(name STREQUAL "fs")
    target_link_libraries(${name} PRIVATE fatfs)
    target_include_directories(${name} SYSTEM PRIVATE ${FATFS_BD})
  endif()

  target_link_options(${name} PRIVATE
    ${LINKFLAGS} --ld-path=${LLD}
    -Wl,-T,${CMAKE_SOURCE_DIR}/user/user.ld
    -Wl,-Map,${CMAKE_BINARY_DIR}/${name}.map)
  set_target_properties(${name} PROPERTIES LINK_DEPENDS ${CMAKE_SOURCE_DIR}/user/user.ld)

  embed_elf(${name})
endforeach()

# ---------------------------------------------------------------------------
# doom. The upstream doomgeneric tree is staged out of the read-only Nix store
# into the build dir with Pneuma's fixups applied; the platform layer
# (user/doomgeneric_rv.c) is *our* source and compiles alongside it, so editing
# it is a one-file rebuild instead of a full Nix derivation.
#
# The staging script prints the .c files it produced. That is not project-source
# globbing: upstream is a pinned store path, so the list changes only when the
# pinned rev does.
# ---------------------------------------------------------------------------
set(DOOM_BD ${CMAKE_BINARY_DIR}/doomgeneric)
execute_process(
  COMMAND ${SCRIPTS}/patch-doomgeneric ${DOOMGENERIC_SRC} ${DOOM_BD}
  OUTPUT_VARIABLE _doom_srcs
  OUTPUT_STRIP_TRAILING_WHITESPACE
  COMMAND_ERROR_IS_FATAL ANY)
string(REPLACE "\n" ";" DOOM_SRCS "${_doom_srcs}")

add_executable(doom ${DOOM_SRCS} ${CMAKE_SOURCE_DIR}/user/doomgeneric_rv.c)
set_target_properties(doom PROPERTIES
  SUFFIX ".elf"
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_compile_options(doom PRIVATE ${DOOM_C})
# libc/include ahead of the staged tree, matching the flake's search order.
target_include_directories(doom PRIVATE ${LIBC_INC} ${DOOM_BD})
target_include_directories(doom SYSTEM PRIVATE ${DRV_INC})
target_link_libraries(doom PRIVATE libc)
target_link_options(doom PRIVATE
  ${LINKFLAGS} --ld-path=${LLD}
  -Wl,-T,${CMAKE_SOURCE_DIR}/user/user.ld
  -Wl,-Map,${CMAKE_BINARY_DIR}/doom.map)
set_target_properties(doom PROPERTIES LINK_DEPENDS ${CMAKE_SOURCE_DIR}/user/user.ld)

embed_elf(doom)

# ---------------------------------------------------------------------------
# Kernel: all kernel/*.cc + every embedded blob, linked with kernel.ld.
# ---------------------------------------------------------------------------
file(GLOB_RECURSE KERNEL_SRCS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/kernel/*.cc)
get_property(EMBEDS GLOBAL PROPERTY PNEUMA_EMBEDS)

add_executable(kernel ${KERNEL_SRCS} ${EMBEDS})
set_target_properties(kernel PROPERTIES
  SUFFIX ".elf"
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_compile_options(kernel PRIVATE ${KERNEL_CXX})
target_include_directories(kernel PRIVATE ${CMAKE_SOURCE_DIR}/kernel)
target_include_directories(kernel SYSTEM PRIVATE ${DRV_INC})
target_link_options(kernel PRIVATE
  ${LINKFLAGS} ${PIELINK} --ld-path=${LLD}
  -Wl,-T,${CMAKE_SOURCE_DIR}/kernel/kernel.ld
  -Wl,-Map,${CMAKE_BINARY_DIR}/kernel.map)
set_target_properties(kernel PROPERTIES LINK_DEPENDS ${CMAKE_SOURCE_DIR}/kernel/kernel.ld)

# rv64: fail the build if the static-PIE contract regresses. A stamp file (not a
# POST_BUILD hook) so the check is a real graph node -- Image can then depend on
# it, and a kernel that fails the guard never gets an Image written next to it.
if(PNEUMA_PIE)
  add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/pie-guard.stamp
    COMMAND ${SCRIPTS}/pie-guard ${READELF} $<TARGET_FILE:kernel> ${CMAKE_BINARY_DIR}/pie-guard.stamp
    DEPENDS kernel ${SCRIPTS}/pie-guard
    VERBATIM COMMENT "pie-guard kernel.elf")
  set(KERNEL_GUARD ${CMAKE_BINARY_DIR}/pie-guard.stamp)
else()
  set(KERNEL_GUARD "")
endif()

# Flat image for U-Boot `booti` (carries no load address; kernel self-relocates).
# Must be a declared OUTPUT, not a POST_BUILD command, for Ninja to track changes.
add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/Image
  COMMAND ${OBJCOPY} -O binary $<TARGET_FILE:kernel> ${CMAKE_BINARY_DIR}/Image
  DEPENDS kernel ${KERNEL_GUARD}
  VERBATIM COMMENT "objcopy kernel.elf -> Image")
add_custom_target(image ALL DEPENDS ${CMAKE_BINARY_DIR}/Image)

# ---------------------------------------------------------------------------
# Data disk (independent of the kernel; rebuilds only when disk/ changes).
# ---------------------------------------------------------------------------
# DOOM1.WAD is fetched hermetically in flake.nix (as $DOOM_WAD) and injected by
# scripts/run at boot time, not staged into disk/ here.
file(GLOB DISK_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/disk/*)
add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/disk.img
  COMMAND ${SCRIPTS}/mkdisk ${CMAKE_BINARY_DIR}/disk.img ${CMAKE_SOURCE_DIR}/disk
  DEPENDS ${DISK_FILES} ${SCRIPTS}/mkdisk
  VERBATIM COMMENT "mkfs disk.img")
add_custom_target(disk ALL DEPENDS ${CMAKE_BINARY_DIR}/disk.img)
