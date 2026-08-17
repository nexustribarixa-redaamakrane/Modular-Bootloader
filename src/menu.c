/*
 * menu.c - GRUB-style black & white boot menu.
 *
 * Layout:
 *   row 0        title (bright)
 *   row 1        subtitle (dim)
 *   row 2-3      blank
 *   rows 4+      entry list, selected entry reverse-highlighted
 *   bottom       hints + boot countdown
 *
 * Returns: selected index, -1 reboot, -2 shutdown, -3 nothing selected.
 */

#include "mbl.h"

#define VGA_COLS     80u
#define ENTRY_TOP    4
#define VISIBLE      18
#define HINT1_ROW    22
#define HINT2_ROW    23

#define ATTR_NORMAL  0x07u
#define ATTR_HILITE  0x70u
#define ATTR_BRIGHT  0x0Fu
#define ATTR_DIM     0x07u

static int m_strlen(const char *s) {
    int n = 0;
    while (*s++) {
        n++;
    }
    return n;
}

static void center_text(int row, const char *s, uint8_t attr) {
    int col = ((int)VGA_COLS - m_strlen(s)) / 2;
    if (col < 0) {
        col = 0;
    }
    vga_write((uint8_t)row, (uint8_t)col, s, attr);
}

static void draw_frame(const char *title, const char *subtitle) {
    vga_fill(0, 0, ' ', VGA_COLS, ATTR_NORMAL);
    vga_fill(1, 0, ' ', VGA_COLS, ATTR_NORMAL);
    vga_fill(2, 0, ' ', VGA_COLS, ATTR_NORMAL);
    vga_fill(3, 0, ' ', VGA_COLS, ATTR_NORMAL);
    center_text(0, title, ATTR_BRIGHT);
    center_text(1, subtitle, ATTR_DIM);
    vga_fill(HINT1_ROW, 0, ' ', VGA_COLS, ATTR_NORMAL);
    vga_fill(HINT2_ROW, 0, ' ', VGA_COLS, ATTR_NORMAL);
    center_text(HINT1_ROW, "Use the arrow keys to move and Enter to boot.",
                ATTR_DIM);
}

static void draw_entry(int row, const char *name, int selected) {
    uint8_t attr = selected ? ATTR_HILITE : ATTR_NORMAL;
    vga_fill((uint8_t)row, 0, ' ', VGA_COLS, attr);
    vga_write((uint8_t)row, 2, selected ? "> " : "  ", attr);
    vga_write((uint8_t)row, 4, name, attr);
}

static void draw_list(const mbl_entry_t *entries, int count, int sel, int top) {
    int i;
    for (i = 0; i < VISIBLE; i++) {
        int idx = top + i;
        if (idx >= count) {
            break;
        }
        draw_entry(ENTRY_TOP + i, entries[idx].name, idx == sel);
    }
    for (; i < VISIBLE; i++) {
        vga_fill((uint8_t)(ENTRY_TOP + i), 0, ' ', VGA_COLS, ATTR_NORMAL);
    }
}

static void draw_status(const char *name, int remaining) {
    char buf[44];
    int i;
    int n = 0;

    if (remaining > 0) {
        const char *pre = "Booting ";
        while (*pre && n < 43) {
            buf[n++] = *pre++;
        }
        i = 0;
        while (name[i] && n < 43) {
            buf[n++] = name[i++];
        }
        if (remaining > 9) {
            buf[n++] = ' ';
            buf[n++] = 'i';
            buf[n++] = 'n';
            buf[n++] = ' ';
            buf[n++] = '0';
            buf[n++] = (char)('0' + remaining);
            buf[n++] = 's';
        } else {
            buf[n++] = ' ';
            buf[n++] = 'i';
            buf[n++] = 'n';
            buf[n++] = ' ';
            buf[n++] = (char)('0' + remaining);
            buf[n++] = 's';
        }
    } else {
        const char *pre = "Booting ";
        while (*pre && n < 43) {
            buf[n++] = *pre++;
        }
        i = 0;
        while (name[i] && n < 43) {
            buf[n++] = name[i++];
        }
    }
    buf[n] = '\0';
    vga_fill(HINT2_ROW, 0, ' ', VGA_COLS, ATTR_NORMAL);
    center_text(HINT2_ROW, buf, ATTR_DIM);
}

static void draw_no_entries(void) {
    vga_fill(6, 0, ' ', VGA_COLS, ATTR_NORMAL);
    center_text(6, "No bootable entries found.", ATTR_NORMAL);
    vga_fill(8, 0, ' ', VGA_COLS, ATTR_NORMAL);
    center_text(8, "Press Esc to reboot.", ATTR_DIM);
    vga_fill(HINT2_ROW, 0, ' ', VGA_COLS, ATTR_NORMAL);
}

static int menu_handle_key(int key, int count, int *sel, int *top) {
    switch (key) {
        case MBL_KEY_UP:
            (*sel)--;
            if (*sel < 0) {
                *sel = count - 1;
            }
            break;
        case MBL_KEY_DOWN:
            (*sel)++;
            if (*sel >= count) {
                *sel = 0;
            }
            break;
        case MBL_KEY_HOME:
            *sel = 0;
            break;
        case MBL_KEY_END:
            *sel = count - 1;
            break;
        case MBL_KEY_PGUP:
            *sel -= VISIBLE;
            if (*sel < 0) {
                *sel = 0;
            }
            break;
        case MBL_KEY_PGDN:
            *sel += VISIBLE;
            if (*sel >= count) {
                *sel = count - 1;
            }
            break;
        case MBL_KEY_ENTER:
            return 1;             /* boot selected */
        case MBL_KEY_ESC:
        case MBL_KEY_REBOOT:
            return 2;             /* reboot */
        case MBL_KEY_SHUTDOWN:
            return 3;             /* power off */
        default:
            return 0;
    }
    if (*sel < *top) {
        *top = *sel;
    }
    if (*sel >= *top + VISIBLE) {
        *top = *sel - VISIBLE + 1;
    }
    if (*top < 0) {
        *top = 0;
    }
    if (*top > count - VISIBLE) {
        *top = count - VISIBLE;
    }
    if (*top < 0) {
        *top = 0;
    }
    return 0;
}

int menu_run(const mbl_entry_t *entries, int count, int timeout_secs) {
    int sel = 0;
    int top = 0;
    int need_redraw = 1;
    uint8_t last_sec = cmos_read(0);
    int remaining = timeout_secs;

    draw_frame("Modular Bootloader", "OpenWindows OWFS boot manager");

    for (;;) {
        int key;

        if (count <= 0) {
            draw_no_entries();
            key = kbd_wait();
            if (key == MBL_KEY_ESC || key == MBL_KEY_REBOOT) {
                return -1;
            }
            continue;
        }

        if (need_redraw) {
            draw_list(entries, count, sel, top);
            draw_status(entries[sel].name, remaining);
            need_redraw = 0;
        }

        key = kbd_poll();
        if (key != MBL_KEY_NONE) {
            int action = menu_handle_key(key, count, &sel, &top);
            if (action == 1) {
                return sel;
            }
            if (action == 2) {
                return -1;
            }
            if (action == 3) {
                return -2;
            }
            need_redraw = 1;
        }

        if (timeout_secs > 0) {
            uint8_t now = cmos_read(0);
            if (now != last_sec) {
                int delta = (now >= last_sec) ? ((int)now - (int)last_sec)
                                              : ((int)now + 60 - (int)last_sec);
                remaining -= delta;
                last_sec = now;
                if (remaining <= 0) {
                    /* timeout: boot the default (first) entry */
                    return 0;
                }
                draw_status(entries[sel].name, remaining);
            }
        }
    }
}
