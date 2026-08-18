/*
 * kbd.c - UEFI keyboard and time services (replaces PS/2 + CMOS version).
 *
 * Uses EFI_SIMPLE_TEXT_INPUT_PROTOCOL for keyboard input and
 * EFI_RUNTIME_SERVICES->GetTime() for RTC functionality.
 */

#include "efi.h"
#include "mbl.h"

/* Forward: get current RTC seconds via UEFI GetTime */
static uint8_t efi_get_seconds(void);

/* ============================================================================
 * Keyboard
 * ============================================================================ */

int kbd_poll(void)
{
    EFI_STATUS status;
    EFI_INPUT_KEY key;
    int code;

    if (!gConIn) {
        return MBL_KEY_NONE;
    }

    status = gConIn->ReadKeyStroke(gConIn, &key);
    if (EFI_ERROR(status)) {
        return MBL_KEY_NONE;
    }

    /* Check scan codes first (arrow keys, nav) */
    switch (key.ScanCode) {
    case SCAN_UP:       return MBL_KEY_UP;
    case SCAN_DOWN:     return MBL_KEY_DOWN;
    case SCAN_HOME:     return MBL_KEY_HOME;
    case SCAN_END:      return MBL_KEY_END;
    case SCAN_PAGE_UP:  return MBL_KEY_PGUP;
    case SCAN_PAGE_DOWN:return MBL_KEY_PGDN;
    }

    /* Check Unicode characters */
    code = MBL_KEY_NONE;
    switch (key.UnicodeChar) {
    case 0x0D:  code = MBL_KEY_ENTER;    break;  /* Enter */
    case 0x1B:  code = MBL_KEY_ESC;      break;  /* Escape */
    case 'r':
    case 'R':   code = MBL_KEY_REBOOT;   break;
    case 's':
    case 'S':   code = MBL_KEY_SHUTDOWN; break;
    default:    code = MBL_KEY_NONE;      break;
    }

    return code;
}

int kbd_wait(void)
{
    for (;;) {
        int code = kbd_poll();
        if (code != MBL_KEY_NONE) {
            return code;
        }
        /* Busy-wait ~1 second tick using GetTime */
        if (gRT && gRT->GetTime) {
            uint8_t t0 = efi_get_seconds();
            while (efi_get_seconds() == t0) {
                __asm__ volatile ("pause");
            }
        }
    }
}

void kbd_reboot(void)
{
    gRT->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, NULL);
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

/* ============================================================================
 * RTC (replaces CMOS port I/O)
 * ============================================================================ */

uint8_t cmos_read(uint8_t reg)
{
    (void)reg;
    return efi_get_seconds();
}

static uint8_t efi_get_seconds(void)
{
    if (!gRT || !gRT->GetTime) {
        return 0;
    }
    {
        /* EFI_TIME is 12 bytes: Year(2) Month Day DayOfWeek Hour Min Sec ... */
        uint8_t time_buf[16];
        EFI_STATUS status;

        status = gRT->GetTime((void *)time_buf, NULL);

        if (EFI_ERROR(status)) {
            return 0;
        }

        /* EFI_TIME layout (all little-endian):
           Offset 0: Year   (UINT16)
           Offset 2: Month  (UINT8)
           Offset 3: Day    (UINT8)
           Offset 4: Hour   (UINT8)
           Offset 5: Minute (UINT8)
           Offset 6: Second (UINT8)  <-- we want this */
        return time_buf[6];
    }
}
