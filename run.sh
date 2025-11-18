#!/bin/bash
set -xue

# QEMU file path
QEMU=qemu-system-riscv32

# OpenSBI firmware path (ilp32 for riscv32, lp64 for riscv64)
OPENSBI_FW="$QEMU_OPENSBI/share/opensbi/ilp32/generic/firmware/fw_dynamic.bin"

# Build the kernel using nix
nix build

# Start QEMU
$QEMU -machine virt -bios "$OPENSBI_FW" -nographic -serial mon:stdio --no-reboot -kernel result/kernel.elf
