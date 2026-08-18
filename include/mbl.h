#ifndef MBL_H
#define MBL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <sutf/sucs_mode.h>

/* =========================================================================
 * Fixed RAM map
 * ========================================================================= */
#define MBL_FS_BUF         0x00030000u   /* 4 KiB OWFS block staging buffer */
#define MBL_KERNEL_ADDR    0x00200000u   /* kernel load address */
#define MBL_KERNEL_MAX     0x01000000u   /* 16 MiB load cap */
#define MBL_BOOTCONFIG     0x00000510u   /* mbl_boot_config_t (kernel handoff) */

#define MBL_MAGIC_BOOTCFG  0x324C424Du   /* 'MBL2' */

typedef struct {
    uint32_t               magic;        /* MBL_MAGIC_BOOTCFG */
    uint8_t                boot_drive;
    uint8_t                pad[3];
    uint32_t               kernel_size;
    sucs_kernel_boot_config_t sucs_cfg;  /* SuperUnicode kernel boot config */
} mbl_boot_config_t;

/* =========================================================================
 * OpenWindows OWFS on-disk format (read-only bootloader subset)
 * See OpenWindows-Storage/owfs/include/*.h for the authoritative layout.
 * ========================================================================= */
#define OWFS_BLOCK_SIZE        0x1000u
#define OWFS_BLOCK_SHIFT       12u
#define OWFS_PARTITION_LBA     131200u
#define OWFS_PARTITION_OFFSET  (131200UL * 512UL)
#define OWFS_SUPERBLOCK_BLOCK  16u
#define OWFS_INODE_SIZE        0x100u
#define OWFS_INODES_PER_BLOCK  16u
#define OWFS_INODE_TABLE_BLOCKS 256u
#define OWFS_ENTRIES_PER_BLOCK 16u
#define OWFS_NAME_MAX_BYTES    128u
#define OWFS_MAGIC             0x4F574653UL
#define OWFS_ROOT_INODE        0u
#define OWFS_ENTRY_FILE        0x01u
#define OWFS_ENTRY_CATALOG     0x02u
#define OWFS_ENTRY_DELETED     0x80u
#define OWFS_DIRECT_BLOCKS     10u

/* Superblock: block 16 of the partition (byte offset 0x20000). */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version_major;
    uint16_t version_minor;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t free_blocks;
    uint32_t total_inodes;
    uint32_t free_inodes;
    uint32_t bitmap_start_block;
    uint32_t bitmap_block_count;
    uint32_t inode_table_start;
    uint32_t inode_table_blocks;
    uint32_t data_region_start;
    uint32_t root_inode;
    uint32_t mount_count;
    uint32_t state_flags;
    uint64_t fletcher64_bitmap;
    uint8_t  volume_label[64];
    uint32_t checksum;            /* CRC32c of this superblock (field zeroed) */
    uint32_t security_flags;
} owfs_superblock_t;

/* Inode: 256 bytes, 16 per block. */
typedef struct __attribute__((packed)) {
    uint32_t inode_number;
    uint8_t  entry_type;
    uint8_t  name_length;
    uint16_t permissions;
    uint16_t flags;
    uint32_t size_bytes;
    uint32_t block_count;
    uint32_t created_timestamp;
    uint32_t modified_timestamp;
    uint32_t parent_inode;
    uint32_t direct_blocks[OWFS_DIRECT_BLOCKS];
    uint32_t indirect_block;
    uint8_t  name[OWFS_NAME_MAX_BYTES];
    uint16_t owner_uid;
    uint16_t owner_gid;
    uint8_t  security_flags;
    uint8_t  reserved[0xFC - 0xCF];
    uint32_t checksum;            /* CRC32c of this inode (field zeroed) */
} owfs_inode_t;

/* Catalog entry: 256 bytes, 16 per block. */
typedef struct __attribute__((packed)) {
    uint32_t inode_number;
    uint8_t  entry_type;
    uint8_t  name_length;
    uint8_t  name[OWFS_NAME_MAX_BYTES];
    uint8_t  reserved[0xFC - 0x86];
    uint32_t checksum;            /* CRC32c of this entry (field zeroed) */
} owfs_catalog_entry_t;

/* =========================================================================
 * Menu entry list
 * ========================================================================= */
#define MBL_MENU_MAX   32
#define MBL_NAME_MAX   128

typedef struct {
    uint32_t inode;
    uint8_t  type;
    char     name[MBL_NAME_MAX];
    uint32_t size;
} mbl_entry_t;

/* =========================================================================
 * UEFI globals (defined in efi_entry.c, accessible from all modules)
 * ========================================================================= */
#include "efi.h"

/* =========================================================================
 * gop.c (GOP framebuffer renderer)
 * ========================================================================= */
void vga_init_gop(void);
void vga_clear(void);
void vga_goto(uint8_t row, uint8_t col);
void vga_putc(char c);
void vga_puts(const char *s);
void vga_write(uint8_t row, uint8_t col, const char *s, uint8_t attr);
void vga_fill(uint8_t row, uint8_t col, uint8_t ch, uint8_t n, uint8_t attr);

/* =========================================================================
 * kbd.c
 * ========================================================================= */
int kbd_poll(void);     /* 0 if no event, else menu action code */
int kbd_wait(void);     /* blocking variant */
void kbd_reboot(void);
uint8_t rtc_get_seconds(void);

/* Menu action codes */
enum {
    MBL_KEY_NONE = 0,
    MBL_KEY_UP, MBL_KEY_DOWN, MBL_KEY_HOME, MBL_KEY_END,
    MBL_KEY_PGUP, MBL_KEY_PGDN, MBL_KEY_ENTER, MBL_KEY_ESC,
    MBL_KEY_REBOOT, MBL_KEY_SHUTDOWN
};

/* =========================================================================
 * disk.c (UEFI Block I/O)
 * ========================================================================= */
int disk_read_sectors(uint32_t lba, uint32_t linear, uint16_t count);
int disk_read_block(uint32_t block, uint32_t linear);

/* =========================================================================
 * owfs.c
 * ========================================================================= */
int owfs_probe(uint8_t drive);
int owfs_enumerate(mbl_entry_t *entries, int max);
int owfs_load_file(uint32_t inode, uint32_t dest, uint32_t *size_out);
uint8_t owfs_boot_drive(void);

/* =========================================================================
 * menu.c
 * ========================================================================= */
int menu_run(const mbl_entry_t *entries, int count, int timeout_secs);

/* =========================================================================
 * main.c
 * ========================================================================= */
void kmain(void);

#endif /* MBL_H */
