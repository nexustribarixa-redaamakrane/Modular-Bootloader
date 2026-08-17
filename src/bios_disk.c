/*
 * bios_disk.c - sector I/O through the BIOS INT 13h AH=0x42 trampoline.
 * bios_read() is implemented in boot/bootimg.asm: it drops to real mode,
 * runs the extended disk read and returns.  The buffer address is passed
 * as a linear address and split into seg:off by the DAP rules.
 */

#include "mbl.h"

int disk_read_sectors(uint32_t lba, uint32_t linear, uint16_t count) {
    uint16_t seg;
    uint16_t off;
    int rc;

    if (count == 0) {
        return 0;
    }
    seg = (uint16_t)(linear >> 4);
    off = (uint16_t)(linear & 0x0Fu);
    rc = bios_read(lba, 0, seg, off, count);
    return rc;
}

/*
 * disk_read_block - read one OWFS block (4096 bytes = 8 sectors) at
 * partition-relative block number `block` into `linear`.
 * The OWFS partition starts at byte offset 0x10000 (LBA 128), so the
 * partition-relative LBA of a block is 128 + block*8.
 */
int disk_read_block(uint32_t block, uint32_t linear) {
    return disk_read_sectors(0x80u + block * 8u, linear, 8);
}
