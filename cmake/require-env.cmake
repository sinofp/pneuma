# Read <name> from the environment into a CMake variable of the same name,
# failing configure immediately if it's unset. Used for every value that must
# come from the Nix dev shell (CLANG, LLD, FATFS_SRC, DOOMGENERIC_SRC) so a
# missing one is an immediate, clear error instead of a silent fallback to
# whatever a bare command name would resolve to on PATH (Apple's clang, a
# stray nixpkgs one, ...).
#
#   require_env(CLANG)   # sets CLANG from $ENV{CLANG}, or fails configure
include_guard(GLOBAL)

macro(require_env name)
  set(${name} "$ENV{${name}}")
  if(NOT ${name})
    message(FATAL_ERROR "${name} is unset — run inside `nix develop` (or pass -D${name}=...).")
  endif()
endmacro()
