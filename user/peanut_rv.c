// Based on https://github.com/deltabeard/Peanut-GB/blob/master/examples/mini_fb/peanut_minifb.c

#define ENABLE_SOUND   0
#define ENABLE_LCD     1
#define PEANUT_PROFILE 1

#if PEANUT_PROFILE
#define PROF(...) __VA_ARGS__
#else
#define PROF(...)
#endif

/* Import emulator library. */
#include "peanut_gb.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/misc.h>
#include <ui/fb.h>
#include <ui/keys.h>

struct priv_t {
    /* Pointer to allocated memory holding GB file. */
    uint8_t *rom;
    /* Pointer to allocated memory holding save file. */
    uint8_t *cart_ram;
    /* Size of the cart_ram in bytes. */
    size_t save_size;

    /* Frame buffer */
    uint32_t fb[LCD_HEIGHT][LCD_WIDTH];
};

/**
 * Returns a byte from the ROM file at the given address.
 */
uint8_t gb_rom_read(struct gb_s *gb, const uint_fast32_t addr) {
    const struct priv_t *const p = gb->direct.priv;
    return p->rom[addr];
}

/**
 * Returns a byte from the cartridge RAM at the given address.
 */
uint8_t gb_cart_ram_read(struct gb_s *gb, const uint_fast32_t addr) {
    const struct priv_t *const p = gb->direct.priv;
    return p->cart_ram[addr];
}

/**
 * Writes a given byte to the cartridge RAM at the given address.
 */
void gb_cart_ram_write(struct gb_s *gb, const uint_fast32_t addr, const uint8_t val) {
    const struct priv_t *const p = gb->direct.priv;
    p->cart_ram[addr] = val;
}

/**
 * Returns a pointer to the allocated space containing the ROM. Must be freed.
 */
uint8_t *read_rom_to_ram(const char *file_name) {
    FILE *rom_file = fopen(file_name, "rb");
    size_t rom_size;
    uint8_t *rom = NULL;

    if (rom_file == NULL)
        return NULL;

    fseek(rom_file, 0, SEEK_END);
    rom_size = ftell(rom_file);
    fseek(rom_file, 0, SEEK_SET);
    rom = malloc(rom_size);

    if (fread(rom, sizeof(uint8_t), rom_size, rom_file) != rom_size) {
        free(rom);
        fclose(rom_file);
        return NULL;
    }

    fclose(rom_file);
    return rom;
}

/**
 * Returns if ESC is pressed
 */
bool process_joypad(uint8_t *joypad, virtio_input_event *ev) {
    bool esc = false;
    while (poll_event(ev)) {
        switch (ev->value) {
        case KEY_PRESS:
            switch (ev->code) {
            case SCANCODE_ESC:
                esc = true;
                break;
            case SCANCODE_ENTER:
                *joypad &= ~JOYPAD_START;
                break;
            case SCANCODE_TAB:
                *joypad &= ~JOYPAD_SELECT;
                break;
            case SCANCODE_X:
                *joypad &= ~JOYPAD_A;
                break;
            case SCANCODE_Z:
                *joypad &= ~JOYPAD_B;
                break;
            case SCANCODE_UP:
                *joypad &= ~JOYPAD_UP;
                break;
            case SCANCODE_DOWN:
                *joypad &= ~JOYPAD_DOWN;
                break;
            case SCANCODE_LEFT:
                *joypad &= ~JOYPAD_LEFT;
                break;
            case SCANCODE_RIGHT:
                *joypad &= ~JOYPAD_RIGHT;
                break;
            default:
                break;
            }
            break;

        case KEY_RELEASE:
            switch (ev->code) {
            case SCANCODE_ENTER:
                *joypad |= JOYPAD_START;
                break;
            case SCANCODE_TAB:
                *joypad |= JOYPAD_SELECT;
                break;
            case SCANCODE_X:
                *joypad |= JOYPAD_A;
                break;
            case SCANCODE_Z:
                *joypad |= JOYPAD_B;
                break;
            case SCANCODE_UP:
                *joypad |= JOYPAD_UP;
                break;
            case SCANCODE_DOWN:
                *joypad |= JOYPAD_DOWN;
                break;
            case SCANCODE_LEFT:
                *joypad |= JOYPAD_LEFT;
                break;
            case SCANCODE_RIGHT:
                *joypad |= JOYPAD_RIGHT;
                break;
            default:
                break;
            }
            break;

        default:
            break;
        }
    }
    return esc;
}

/**
 * Ignore all errors.
 */
void gb_error(struct gb_s *gb, const enum gb_error_e gb_err, const uint16_t val) {
    const char *gb_err_str[GB_INVALID_MAX] = { "UNKNOWN", "INVALID OPCODE", "INVALID READ", "INVALID WRITE",
                                               "HALT FOREVER" };
    struct priv_t *priv = gb->direct.priv;

    fprintf(stderr, "Error %d occurred: %s at %04X\n. Exiting.\n", gb_err, gb_err_str[gb_err], val);

    /* Free memory and then exit. */
    free(priv->cart_ram);
    free(priv->rom);
    exit(EXIT_FAILURE);
}

#if ENABLE_LCD
/**
 * Draws scanline into framebuffer.
 */
void lcd_draw_line(struct gb_s *gb, const uint8_t pixels[160], const uint_fast8_t line) {
    struct priv_t *priv = gb->direct.priv;
    const uint32_t palette[] = { 0xFFFFFF, 0xA5A5A5, 0x525252, 0x000000 };

    for (unsigned int x = 0; x < LCD_WIDTH; x++)
        priv->fb[line][x] = palette[pixels[x] & 3];
}
#endif

int main(int argc, char **argv) {
    /* Must be freed */
    char *rom_file_name = NULL;
    static struct gb_s gb;
    static struct priv_t priv;
    enum gb_init_error_e ret;
    virtio_input_event ev;
    double next_frame;
    const double target_speed_ms = 1000. / VERTICAL_SYNC;
    unsigned now = 0;
    PROF(unsigned acc_emu = 0, acc_fb = 0, acc_poll = 0, acc_sleep = 0, frames = 0, report_start, report_end,
         before_emu, after_emu, after_fb;)

    // switch (argc) {
    // case 2:
    //     rom_file_name = argv[1];
    //     break;

    // default:
    //     fprintf(stderr, "%s ROM\n", argv[0]);
    //     exit(EXIT_FAILURE);
    // }
    rom_file_name = "RHYTHM.GB";

    puts("Hello from Peanut-GB!");

    /* Copy input ROM file to allocated memory. */
    if ((priv.rom = read_rom_to_ram(rom_file_name)) == NULL) {
        printf("%d: %s\n", __LINE__); //, strerror(errno));
        exit(EXIT_FAILURE);
    }

    /* Initialise context. */
    ret = gb_init(&gb, &gb_rom_read, &gb_cart_ram_read, &gb_cart_ram_write, &gb_error, &priv);

    puts("Peanut-GB init finished!");

    if (ret != GB_INIT_NO_ERROR) {
        printf("Error: %d\n", ret);
        exit(EXIT_FAILURE);
    }

    ret = gb_get_save_size_s(&gb, &priv.save_size);
    if (ret < 0) {
        printf("Unable to get save size\n");
        exit(EXIT_FAILURE);
    }

    /* Only attempt to load a save file if the ROM actually supports saves.*/
    if (priv.save_size > 0)
        // TODO read_cart_ram_file(save_file_name, &priv.cart_ram, priv.save_size);
        priv.cart_ram = malloc(priv.save_size);

#if ENABLE_LCD
    gb_init_lcd(&gb, &lcd_draw_line);
    // gb.direct.interlace = true;
#endif

    if (fb_open(priv.fb, LCD_WIDTH, LCD_HEIGHT) < 0)
        return EXIT_FAILURE;

    for (PROF(report_start =) next_frame = get_ticks_ms();;) {
        next_frame += target_speed_ms;
        PROF(before_emu = get_ticks_ms();)

        /* Execute CPU cycles until the screen has to be redrawn. */
        gb_run_frame(&gb);
        PROF(after_emu = get_ticks_ms();)

        fb_update();
        PROF(after_fb = get_ticks_ms();)

        bool esc = process_joypad(&gb.direct.joypad, &ev);
        now = get_ticks_ms();

        PROF(acc_emu += after_emu - before_emu; acc_fb += after_fb - after_emu; acc_poll += now - after_fb;)

        if (esc)
            break;

        /* If it took more than the maximum allowed time to draw frame,
         * do not delay.
         * Interlaced mode could be enabled here to help speed up
         * drawing.
         */
        if (now < next_frame) {
            sleep_ms(next_frame - now);
            PROF(acc_sleep += get_ticks_ms() - now;)
        }

        PROF(if (60 == ++frames) {
            report_end = get_ticks_ms();
            printf("60f: wall %d (target %d), emu %d, fb %d, poll %d, sleep %d\n", report_end - report_start,
                   (int) (60 * target_speed_ms), acc_emu, acc_fb, acc_poll, acc_sleep);
            acc_emu = acc_fb = acc_poll = acc_sleep = frames = 0;
            report_start = report_end;
        })
    }

    fb_close();
    free(priv.cart_ram);
    free(priv.rom);

    return EXIT_SUCCESS;
}
