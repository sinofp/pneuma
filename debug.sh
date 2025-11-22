#!/bin/bash

# Get the compilation directory from debug info
COMP_DIR=$(llvm-dwarfdump result/kernel.elf | grep DW_AT_comp_dir | head -1 | sed -E 's/.*"(.*)".*/\1/')

echo "Starting LLDB with source mapping:"
echo "  $COMP_DIR -> $(pwd)"

# Start LLDB with source path mapping
lldb result/kernel.elf \
  -o "settings set target.source-map \"$COMP_DIR\" \"$(pwd)\"" \
  -o 'gdb-remote localhost:1234'
