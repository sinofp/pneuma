ENTRY(boot)

SECTIONS {
    . = 0x80200000;
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
    }

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
