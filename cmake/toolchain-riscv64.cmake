# CMake cross-toolchain for the rv64 Pneuma kernel.
# Uses clang from the Nix dev shell ($CLANG)
#
#   cmake -B build/rv64 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-riscv64.cmake

include(${CMAKE_CURRENT_LIST_DIR}/require-env.cmake)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv64)

set(PNEUMA_ARCH   riscv64)
set(PNEUMA_TARGET riscv64-unknown-elf)
set(PNEUMA_MARCH  rv64imad_zicsr_zifencei)
set(PNEUMA_MABI   lp64d)
set(PNEUMA_PIE    ON)

require_env(CLANG)
set(CMAKE_C_COMPILER   ${CLANG})
set(CMAKE_CXX_COMPILER ${CLANG})

# Arch selection on every compile *and* link. -fPIE is rv64-only: combined with
# the boot-time relocation loop in main.cc, one static-PIE image boots at any
# DRAM base (virt @ 0x80200000, jh7110 @ 0x40200000).
set(_arch "--target=${PNEUMA_TARGET} -march=${PNEUMA_MARCH} -mabi=${PNEUMA_MABI} -fPIE")
set(CMAKE_C_FLAGS_INIT   "${_arch}")
set(CMAKE_CXX_FLAGS_INIT "${_arch}")

# The probe has no crt/libc to link a full executable against.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Find host tools (llvm-objcopy, ld.lld, mkfs.fat...) on PATH, not under a sysroot.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
