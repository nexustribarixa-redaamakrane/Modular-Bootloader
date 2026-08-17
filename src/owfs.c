/*
 * owfs.c - read-only OpenWindows OWFS volume driver for the bootloader.
 *
 * Implements just enough of the on-disk format to list the root catalog
 * and stream a regular file to memory:
 *   - CRC32c (Castagnoli) checksums for superblock / inodes / catalog
 *   - inode table + direct/indirect block maps
 *   - SUTF-8 (SuperUnicode) name decoding via the vendored sutf8 module
 *
 * Layout recap (see OpenWindows-Storage/owfs/include/owfs_types.h):
 *   partition starts at byte offset 0x10000 (LBA 128, 8 sectors per block)
 *   block 16     : superblock
 *   block 17..   : allocation bitmap (not needed for reading)
 *   then inode table (256 blocks), then the data region.
 */

#include "mbl.h"
#include <sutf/sutf8.h>

#define OWFS_CATALOG_ENTRY_SIZE 0x100u
#define OWFS_SUPERBLOCK_CK      0x84u

static owfs_superblock_t g_sb;
static uint8_t g_drive;
static uint8_t g_block_buf[OWFS_BLOCK_SIZE];

/* -------------------------------------------------------------------------
 * CRC32c (Castagnoli 0x82F63B78), byte-wise - mirrors ow_checksum.c
 * ------------------------------------------------------------------------- */
static uint32_t crc32c_byte(uint32_t crc, uint8_t b) {
    int i;
    crc ^= b;
    for (i = 0; i < 8; i++) {
        if (crc & 1u) {
            crc = (crc >> 1) ^ 0x82F63B78u;
        } else {
            crc = crc >> 1;
        }
    }
    return crc;
}

/* CRC over a packed struct with the checksum field treated as zero. */
static uint32_t crc32c_struct(const void *data, uint32_t size, uint32_t ck_off) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t i;
    for (i = 0; i < size; i++) {
        uint8_t b = p[i];
        if (i >= ck_off && i < ck_off + 4u) {
            b = 0;
        }
        crc = crc32c_byte(crc, b);
    }
    return ~crc;
}

/* -------------------------------------------------------------------------
 * small helpers
 * ------------------------------------------------------------------------- */
static uint32_t read32(const void *p) {
    const uint8_t *b = (const uint8_t *)p;
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static void copy_bytes(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) {
        *d++ = *s++;
    }
}

/* Decode an SUTF-8 name into printable ASCII ('?' for non-ASCII). */
static void owfs_name_to_ascii(char *out, const uint8_t *name, uint8_t len) {
    uint32_t i = 0;
    uint32_t o = 0;
    while (i < len && o < MBL_NAME_MAX - 1u) {
        sucs_char_t cp = SUCS_INVALID_CODEPOINT;
        size_t consumed = sutf8_decode_char(name + i, (size_t)(len - i), &cp);
        if (consumed == 0) {
            out[o++] = '?';
            i++;
        } else {
            if (cp <= 0x7Fu) {
                out[o++] = (char)cp;
            } else {
                out[o++] = '?';
            }
            i += (uint32_t)consumed;
        }
    }
    out[o] = '\0';
}

/* -------------------------------------------------------------------------
 * probe / read / enumerate
 * ------------------------------------------------------------------------- */
int owfs_probe(uint8_t drive) {
    g_drive = drive;
    if (disk_read_block(OWFS_SUPERBLOCK_BLOCK, (uint32_t)g_block_buf) != 0) {
        return -1;
    }
    if (read32(g_block_buf) != OWFS_MAGIC) {
        return -2;
    }
    if (read32(g_block_buf + 8) != OWFS_BLOCK_SIZE) {
        return -3;
    }
    if (crc32c_struct(g_block_buf, OWFS_BLOCK_SIZE, OWFS_SUPERBLOCK_CK) !=
        read32(g_block_buf + OWFS_SUPERBLOCK_CK)) {
        return -4;
    }
    copy_bytes(&g_sb, g_block_buf, sizeof(owfs_superblock_t));
    return 0;
}

static int owfs_read_inode(uint32_t num, owfs_inode_t *ino) {
    uint32_t block;
    const uint8_t *src;
    if (num >= g_sb.total_inodes) {
        return -1;
    }
    block = g_sb.inode_table_start + (num >> 4);
    if (disk_read_block(block, (uint32_t)g_block_buf) != 0) {
        return -2;
    }
    src = g_block_buf + (num & 0x0Fu) * OWFS_INODE_SIZE;
    copy_bytes(ino, src, sizeof(owfs_inode_t));
    if (ino->entry_type == 0) {
        return -3;
    }
    if (crc32c_struct(ino, sizeof(owfs_inode_t), 0xFCu) != ino->checksum) {
        return -4;
    }
    return 0;
}

static int owfs_blockmap_get(const owfs_inode_t *inode, uint32_t idx,
                             uint32_t *out_block) {
    if (idx >= inode->block_count) {
        return -1;
    }
    if (idx < OWFS_DIRECT_BLOCKS) {
        if (inode->direct_blocks[idx] == 0) {
            return -1;
        }
        *out_block = inode->direct_blocks[idx];
        return 0;
    }
    if (inode->indirect_block == 0) {
        return -1;
    }
    if (disk_read_block(inode->indirect_block, MBL_FS_BUF) != 0) {
        return -2;
    }
    *out_block = read32((const void *)(MBL_FS_BUF + (idx - OWFS_DIRECT_BLOCKS) * 4u));
    if (*out_block == 0) {
        return -1;
    }
    return 0;
}

int owfs_enumerate(mbl_entry_t *entries, int max) {
    owfs_inode_t root;
    uint32_t idx;
    int count = 0;

    if (owfs_read_inode(OWFS_ROOT_INODE, &root) != 0) {
        return -1;
    }
    if (!(root.entry_type & OWFS_ENTRY_CATALOG)) {
        return -2;
    }

    for (idx = 0; idx < root.block_count; idx++) {
        uint32_t bnum;
        uint32_t e;
        if (owfs_blockmap_get(&root, idx, &bnum) != 0) {
            return -3;
        }
        if (disk_read_block(bnum, MBL_FS_BUF) != 0) {
            return -4;
        }
        for (e = 0; e < OWFS_ENTRIES_PER_BLOCK; e++) {
            const owfs_catalog_entry_t *ce =
                (const owfs_catalog_entry_t *)(MBL_FS_BUF + e * OWFS_CATALOG_ENTRY_SIZE);
            if (ce->entry_type != 0 && !(ce->entry_type & OWFS_ENTRY_DELETED)) {
                if (crc32c_struct(ce, sizeof(owfs_catalog_entry_t), 0xFCu) !=
                    ce->checksum) {
                    continue;
                }
                if (count >= max) {
                    return count;
                }
                entries[count].inode = ce->inode_number;
                entries[count].type = ce->entry_type;
                entries[count].size = 0;
                owfs_name_to_ascii(entries[count].name, ce->name,
                                   ce->name_length);
                count++;
            }
        }
    }
    return count;
}

int owfs_load_file(uint32_t inode_num, uint32_t dest, uint32_t *size_out) {
    owfs_inode_t ino;
    uint32_t idx;
    uint32_t dst = dest;

    if (owfs_read_inode(inode_num, &ino) != 0) {
        return -1;
    }
    if (!(ino.entry_type & OWFS_ENTRY_FILE)) {
        return -2;
    }
    if (ino.size_bytes > MBL_KERNEL_MAX) {
        return -3;
    }

    for (idx = 0; idx < ino.block_count; idx++) {
        uint32_t bnum;
        uint32_t n = OWFS_BLOCK_SIZE;
        if (owfs_blockmap_get(&ino, idx, &bnum) != 0) {
            return -4;
        }
        if (disk_read_block(bnum, MBL_FS_BUF) != 0) {
            return -5;
        }
        if (idx + 1u == ino.block_count) {
            uint32_t filled = idx * OWFS_BLOCK_SIZE;
            uint32_t rem = ino.size_bytes - filled;
            if (rem < OWFS_BLOCK_SIZE) {
                n = rem;
            }
        }
        copy_bytes((void *)dst, (const void *)MBL_FS_BUF, n);
        dst += n;
    }

    if (size_out) {
        *size_out = ino.size_bytes;
    }
    return 0;
}

uint8_t owfs_boot_drive(void) {
    return g_drive;
}
