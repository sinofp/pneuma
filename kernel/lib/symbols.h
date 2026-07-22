#pragma once

#include <stdint.h>

extern "C" {
// BSS and stack
extern uint8_t __bss[], __bss_end[], __image_end[], __stack_top[];

// Memory regions
extern uint8_t __kernel_base[], __free_ram[];

// Embedded user program binaries (from *.bin.o files)
extern uint8_t _binary_fs_bin_start[], _binary_fs_bin_size[];
extern uint8_t _binary_virtio_blk_bin_start[], _binary_virtio_blk_bin_size[];
extern uint8_t _binary_virtio_input_bin_start[], _binary_virtio_input_bin_size[];
extern uint8_t _binary_virtio_gpu_bin_start[], _binary_virtio_gpu_bin_size[];
extern uint8_t _binary_shell_bin_start[], _binary_shell_bin_size[];
extern uint8_t _binary_a_bin_start[], _binary_a_bin_size[];
extern uint8_t _binary_b_bin_start[], _binary_b_bin_size[];
extern uint8_t _binary_libc_test_bin_start[], _binary_libc_test_bin_size[];
extern uint8_t _binary_doom_bin_start[], _binary_doom_bin_size[];
}
