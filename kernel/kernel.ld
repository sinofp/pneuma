ENTRY(boot)

/* Absolute link base. The rv64 kernel is a static-PIE: it is always LINKED here
   but may be LOADED at a different DRAM base (jh7110 @ 0x40200000). boot()'s
   relocation loop derives the load bias as runtime(__kernel_base) - __link_base
   and applies the .rela.dyn R_RISCV_RELATIVE fixups. Kept absolute (not a section
   symbol) so %hi/%lo against it stay link-time constants, not delta-adjusted. */
__link_base = 0x80200000;

SECTIONS {
    . = __link_base;
    __kernel_base = .;

    .text :{
        KEEP(*(.text.boot));
        *(.text .text.*);
    }

    .rodata : ALIGN(4) {
        *(.rodata .rodata.*);
    }

    .data : ALIGN(4) {
        *(.data .data.*);
        *(.got .got.plt .got.*);
    }

    /* Static-PIE fixups (empty for the rv32 ET_EXEC build). Kept inside the
       loaded image, before .bss/the stack gap, so the flat Image carries them.
       R_RISCV_RELATIVE entries resolve from offset+addend alone, so the rest of
       the dynamic metadata (.dynsym/.dynstr/hashes/.dynamic) serves only a
       dynamic loader we don't have. */
    .rela.dyn : ALIGN(8) {
        __rela_start = .;
        *(.rela .rela.* .rela.dyn);
        __rela_end = .;
    }

    /DISCARD/ : { *(.interp .dynamic) *(.dynsym .dynstr .hash .gnu.hash) }

    .bss : ALIGN(4) {
        __bss = .;
        *(.bss .bss.* .sbss .sbss.*);
        __bss_end = .;
    }

    . = ALIGN(4);
    __image_end = .;

    /* One 128 KB boot stack per hart (pairs with hart::MAX and boot()'s hartid<<17 offset):
       hart h's stack top is __stack_top - h * 128K, so hart 0 owns the topmost slot. */
    . = ALIGN(16);
    . += 5 * 128 * 1024;
    __stack_top = .;

    . = ALIGN(4096);
    __free_ram = .;

    /* The free ram end is parsed from device tree */
}
