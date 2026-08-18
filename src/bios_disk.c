/*
 * bios_disk.c - sector I/O through the UEFI Block I/O protocol.
 *
 * This replaces the BIOS INT 13h trampoline with direct UEFI calls.
 * The Block I/O protocol handle is discovered during efi_main().
 */

#include "efi.h"
#include "mbl.h"

int disk_read_sectors(uint32_t lba, uint32_t linear, uint16_t count)
{
    EFI_STATUS status;

    if (count == 0) {
        return 0;
    }
    if (!gBlockIO || !gBlockIO->ReadBlocks) {
        return -1;
    }

    status = gBlockIO->ReadBlocks(
        gBlockIO,
        gBlockIO->Media->MediaId,
        (EFI_LBA)lba,
        (UINTN)count * 512u,
        (void *)(UINTN)linear
    );

    return EFI_ERROR(status) ? -1 : 0;
}

/*
 * disk_read_block - read one OWFS block (4096 bytes = 8 sectors) at
 * partition-relative block number `block` into `linear`.
 * The OWFS partition starts at LBA OWFS_PARTITION_LBA (131200), so the
 * LBA of a block is OWFS_PARTITION_LBA + block*8.
 */
int disk_read_block(uint32_t block, uint32_t linear)
{
    return disk_read_sectors(OWFS_PARTITION_LBA + block * 8u, linear, 8);
}
