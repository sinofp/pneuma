# CMake cross-toolchain for the rv32 Pneuma kernel.
# Uses clang from the Nix dev shell ($CLANG)
#
#   cmake -B build/rv32 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-riscv32.cmake

include(${CMAKE_CURRENT_LIST_DIR}/require-env.cmake)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv32)

set(PNEUMA_ARCH   riscv32)
set(PNEUMA_TARGET riscv32-unknown-elf)
set(PNEUMA_MARCH  rv32imad_zicsr_zifencei)
set(PNEUMA_MABI   ilp32d)
set(PNEUMA_PIE    OFF)

require_env(CLANG)
set(CMAKE_C_COMPILER   ${CLANG})
set(CMAKE_CXX_COMPILER ${CLANG})

# rv32 stays a fixed ET_EXEC (not compiled -fPIE): it always boots at its link
# address, so it needs no self-relocation.
set(_arch "--target=${PNEUMA_TARGET} -march=${PNEUMA_MARCH} -mabi=${PNEUMA_MABI}")
set(CMAKE_C_FLAGS_INIT   "${_arch}")
set(CMAKE_CXX_FLAGS_INIT "${_arch}")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
