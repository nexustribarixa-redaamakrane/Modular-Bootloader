/*
 * kbd.c - polling PS/2 keyboard driver (scancode set 1) and CMOS RTC.
 *
 * The bootloader runs in protected mode with no IDT, so interrupts stay
 * disabled and the keyboard controller is polled via ports 0x60/0x64.
 * E0-prefixed extended scancodes (arrows, numpad Enter) are folded into
 * the same actions as their basic equivalents.
 *
 * NOTE: HLT is never used here - with interrupts masked HLT would never
 * wake, so all waits are busy loops over the controller status port.
 */

#include "mbl.h"

#define KB_STATUS   0x64u
#define KB_DATA     0x60u
#define KB_CMD      0x64u

#define STATUS_OUT_FULL  0x01u
#define STATUS_IN_FULL   0x02u

static uint8_t g_e0;

static int kbd_has_data(void) {
    return (inb(KB_STATUS) & STATUS_OUT_FULL) != 0;
}

int kbd_poll(void) {
    uint8_t sc;
    int code;

    if (!kbd_has_data()) {
        return MBL_KEY_NONE;
    }
    sc = inb(KB_DATA);

    if (sc == 0xE0) {
        g_e0 = 1;
        return MBL_KEY_NONE;
    }
    if (sc == 0xE1) {
        g_e0 = 0;
        return MBL_KEY_NONE;
    }
    if (sc & 0x80) {
        g_e0 = 0;
        return MBL_KEY_NONE;
    }

    code = MBL_KEY_NONE;
    switch (sc) {
        case 0x48: code = MBL_KEY_UP;        break;
        case 0x50: code = MBL_KEY_DOWN;      break;
        case 0x47: code = MBL_KEY_HOME;      break;
        case 0x4F: code = MBL_KEY_END;       break;
        case 0x49: code = MBL_KEY_PGUP;      break;
        case 0x51: code = MBL_KEY_PGDN;      break;
        case 0x1C: code = MBL_KEY_ENTER;     break;
        case 0x39: code = MBL_KEY_ENTER;     break;   /* space = select */
        case 0x01: code = MBL_KEY_ESC;       break;
        case 0x13: code = MBL_KEY_REBOOT;    break;   /* 'r' */
        case 0x1F: code = MBL_KEY_SHUTDOWN;  break;   /* 's' */
        default:   code = MBL_KEY_NONE;      break;
    }
    g_e0 = 0;
    return code;
}

int kbd_wait(void) {
    int code;
    for (;;) {
        code = kbd_poll();
        if (code != MBL_KEY_NONE) {
            return code;
        }
    }
}

void kbd_reboot(void) {
    /* 8042 system reset request */
    while (inb(KB_STATUS) & STATUS_IN_FULL) {
        /* wait for input buffer to drain */
    }
    outb(KB_CMD, 0xFE);
    for (;;) {
        /* reset is imminent; park here if the controller ignores it */
    }
}

/*
 * cmos_read - read a CMOS register.  Waits for the update-in-progress
 * flag to clear before reading so the value is stable.
 */
uint8_t cmos_read(uint8_t reg) {
    uint8_t stat;
    do {
        outb(0x70u, 0x0Au);
        stat = inb(0x71u);
    } while (stat & 0x80u);
    outb(0x70u, reg);
    return inb(0x71u);
}
