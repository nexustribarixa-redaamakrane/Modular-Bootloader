#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""owfs_mkfs.py - OpenWindows OWFS volume formatter (mkfs).

Replicates the on-disk layout produced by OpenWindows-Storage's
`owfs_format_volume()` + `owfs_catalog_insert()`:

    partition starts at byte offset 0x10000 (LBA 128)
    block 16                       superblock
    block 17..17+bitmap_blocks-1   allocation bitmap (LSB-first, covers
                                   only the data region)
    then 256 inode-table blocks    16 inodes/block, 256 bytes each
    then the data region

Checksums are CRC32c (Castagnoli, poly 0x82F63B78) computed with the
checksum field zeroed; the superblock also carries a Fletcher-64 of the
bitmap region (matching ow_checksum.c / owfs_bitmap.c).
"""

import struct

BLOCK = 0x1000
SUPER_BLOCK = 16
INODE_TABLE_BLOCKS = 256
INODES_PER_BLOCK = 16
ENTRIES_PER_BLOCK = 16
MAGIC = 0x4F574653
TOTAL_INODES = 0x1000
ROOT_INODE = 0
ENTRY_FILE = 0x01
ENTRY_CATALOG = 0x02
ENTRY_DELETED = 0x80
MODE_DEFAULT_FILE = 0x1A4
MODE_DEFAULT_DIR = 0x1FF
BLOCK_START = 0x10000

CRC_POLY = 0x82F63B78


# ---------------------------------------------------------------------------
# CRC32c helpers
# ---------------------------------------------------------------------------
def crc32c(data, initial=0xFFFFFFFF):
    crc = initial
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ (CRC_POLY & -(crc & 1))
    return crc


def crc32c_struct(buf, ck_off):
    """CRC over `buf` with the 4 checksum bytes treated as zero."""
    b = bytearray(buf)
    for i in range(ck_off, ck_off + 4):
        b[i] = 0
    return crc32c(b) ^ 0xFFFFFFFF


def fletcher64(blocks):
    sum1 = 0
    sum2 = 0
    for block in blocks:
        for i in range(0, BLOCK, 4):
            word = int.from_bytes(block[i:i + 4], "little")
            sum1 = (sum1 + word) % 0xFFFFFFFF
            sum2 = (sum2 + sum1) % 0xFFFFFFFF
    return (sum2 << 32) | sum1


# ---------------------------------------------------------------------------
# low-level accessors
# ---------------------------------------------------------------------------
def put32(buf, off, val):
    struct.pack_into("<I", buf, off, val & 0xFFFFFFFF)


def put16(buf, off, val):
    struct.pack_into("<H", buf, off, val & 0xFFFF)


def put64(buf, off, val):
    struct.pack_into("<Q", buf, off, val & 0xFFFFFFFFFFFFFFFF)


def block_off(block):
    return BLOCK_START + block * BLOCK


def inode_off(sb, num):
    block = sb["inode_table_start"] + (num >> 4)
    return block_off(block) + (num & 0x0F) * 0x100


class Volume:
    def __init__(self, image, total_blocks, label):
        self.image = image
        self.total_blocks = total_blocks
        fixed_overhead = SUPER_BLOCK + 1 + INODE_TABLE_BLOCKS  # 273
        self.bitmap_blocks = (total_blocks - fixed_overhead + 32768) // 32769
        self.sb = {
            "magic": MAGIC,
            "version_major": 1,
            "version_minor": 2,
            "block_size": BLOCK,
            "total_blocks": total_blocks,
            "free_blocks": 0,
            "total_inodes": TOTAL_INODES,
            "free_inodes": TOTAL_INODES - 1,
            "bitmap_start_block": SUPER_BLOCK + 1,
            "bitmap_block_count": self.bitmap_blocks,
            "inode_table_start": SUPER_BLOCK + 1 + self.bitmap_blocks,
            "inode_table_blocks": INODE_TABLE_BLOCKS,
            "data_region_start": SUPER_BLOCK + 1 + self.bitmap_blocks
                                 + INODE_TABLE_BLOCKS,
            "root_inode": ROOT_INODE,
            "mount_count": 0,
            "state_flags": 0,
            "volume_label": label,
            "security_flags": 0,
        }
        self.sb["free_blocks"] = total_blocks - self.sb["data_region_start"]
        self.root = {
            "inode_number": ROOT_INODE,
            "entry_type": ENTRY_CATALOG,
            "name_length": 1,
            "permissions": MODE_DEFAULT_DIR,
            "blocks": [],
            "block_count": 0,
        }

    def alloc_block(self):
        data_blocks = self.total_blocks - self.sb["data_region_start"]
        for b in range(self.sb["bitmap_block_count"]):
            base = block_off(self.sb["bitmap_start_block"] + b)
            for byte_idx in range(BLOCK):
                val = self.image[base + byte_idx]
                if val == 0xFF:
                    continue
                for bit in range(8):
                    if not (val & (1 << bit)):
                        rel = ((b * BLOCK + byte_idx) * 8) + bit
                        if rel >= data_blocks:
                            raise RuntimeError("OWFS volume is full")
                        self.image[base + byte_idx] = val | (1 << bit)
                        self.sb["free_blocks"] -= 1
                        return self.sb["data_region_start"] + rel
        raise RuntimeError("OWFS volume is full")

    def write_inode(self, num, fields):
        off = inode_off(self.sb, num)
        ino = bytearray(0x100)
        put32(ino, 0x00, fields["inode_number"])
        ino[4] = fields["entry_type"]
        ino[5] = fields["name_length"]
        put16(ino, 6, fields["permissions"])
        put16(ino, 8, fields.get("flags", 0))
        put32(ino, 0x0A, fields.get("size_bytes", 0))
        put32(ino, 0x0E, fields.get("block_count", 0))
        put32(ino, 0x1A, fields.get("parent_inode", 0))
        for i, b in enumerate(fields.get("direct_blocks", [])):
            put32(ino, 0x1E + i * 4, b)
        put32(ino, 0x46, fields.get("indirect_block", 0))
        name = fields.get("name", b"")
        ino[0x4A:0x4A + len(name)] = name
        put32(ino, 0xFC, crc32c_struct(ino, 0xFC))
        self.image[off:off + 0x100] = ino

    def alloc_inode(self, entry_type, name, parent):
        # first slot with entry_type == 0 (root occupies slot 0)
        for num in range(self.sb["total_inodes"]):
            off = inode_off(self.sb, num)
            if self.image[off + 4] == 0:
                self.sb["free_inodes"] -= 1
                return num
        raise RuntimeError("no free inodes")

    def write_catalog_entry(self, base, e, target_inode, entry_type, name):
        off = base + e * 0x100
        ent = bytearray(0x100)
        put32(ent, 0x00, target_inode)
        ent[4] = entry_type
        ent[5] = len(name)
        ent[0x06:0x06 + len(name)] = name
        put32(ent, 0xFC, crc32c_struct(ent, 0xFC))
        self.image[off:off + 0x100] = ent

    def write_root_inode(self):
        self.root["block_count"] = len(self.root["blocks"])
        self.write_inode(
            ROOT_INODE,
            {
                "inode_number": ROOT_INODE,
                "entry_type": ENTRY_CATALOG,
                "name_length": 1,
                "permissions": MODE_DEFAULT_DIR,
                "name": b"/",
                "direct_blocks": self.root["blocks"],
                "block_count": self.root["block_count"],
            },
        )

    def catalog_insert(self, name, target_inode, entry_type):
        for bnum in self.root["blocks"]:
            base = block_off(bnum)
            for e in range(ENTRIES_PER_BLOCK):
                t = self.image[base + e * 0x100 + 4]
                if t == 0 or (t & ENTRY_DELETED):
                    self.write_catalog_entry(base, e, target_inode,
                                             entry_type, name)
                    return
        bnum = self.alloc_block()
        self.root["blocks"].append(bnum)
        self.write_catalog_entry(block_off(bnum), 0, target_inode,
                                 entry_type, name)
        self.write_root_inode()

    def add_file(self, name, data):
        inode_num = self.alloc_inode(ENTRY_FILE, name, ROOT_INODE)
        nblocks = (len(data) + BLOCK - 1) // BLOCK
        direct = []
        indirect = 0
        for i in range(nblocks):
            if i >= 10 and indirect == 0:
                indirect = self.alloc_block()
            block = self.alloc_block()
            self.image[block_off(block):block_off(block) + BLOCK] = data[i * BLOCK:(i + 1) * BLOCK]
            direct.append(block)
        if indirect:
            ibuf = bytearray(BLOCK)
            for i in range(10, len(direct)):
                put32(ibuf, (i - 10) * 4, direct[i])
            self.image[block_off(indirect):block_off(indirect) + BLOCK] = ibuf
            direct = direct[:10]
        self.write_inode(
            inode_num,
            {
                "inode_number": inode_num,
                "entry_type": ENTRY_FILE,
                "name_length": len(name),
                "permissions": MODE_DEFAULT_FILE,
                "size_bytes": len(data),
                "block_count": nblocks,
                "parent_inode": ROOT_INODE,
                "name": name,
                "direct_blocks": direct,
                "indirect_block": indirect,
            },
        )
        self.catalog_insert(name, inode_num, ENTRY_FILE)
        return inode_num

    def write_superblock(self):
        sb = bytearray(BLOCK)
        put32(sb, 0x00, self.sb["magic"])
        put16(sb, 0x04, self.sb["version_major"])
        put16(sb, 0x06, self.sb["version_minor"])
        put32(sb, 0x08, self.sb["block_size"])
        put32(sb, 0x0C, self.sb["total_blocks"])
        put32(sb, 0x10, self.sb["free_blocks"])
        put32(sb, 0x14, self.sb["total_inodes"])
        put32(sb, 0x18, self.sb["free_inodes"])
        put32(sb, 0x1C, self.sb["bitmap_start_block"])
        put32(sb, 0x20, self.sb["bitmap_block_count"])
        put32(sb, 0x24, self.sb["inode_table_start"])
        put32(sb, 0x28, self.sb["inode_table_blocks"])
        put32(sb, 0x2C, self.sb["data_region_start"])
        put32(sb, 0x30, self.sb["root_inode"])
        put32(sb, 0x34, self.sb["mount_count"])
        put32(sb, 0x38, self.sb["state_flags"])
        label = self.sb["volume_label"].encode("utf-8")[:64]
        sb[0x44:0x44 + len(label)] = label
        put32(sb, 0x88, self.sb["security_flags"])

        bitmap_blocks = []
        for b in range(self.sb["bitmap_block_count"]):
            off = block_off(self.sb["bitmap_start_block"] + b)
            bitmap_blocks.append(bytes(self.image[off:off + BLOCK]))
        put64(sb, 0x3C, fletcher64(bitmap_blocks))

        put32(sb, 0x84, crc32c_struct(sb, 0x84))
        self.image[block_off(SUPER_BLOCK):block_off(SUPER_BLOCK) + BLOCK] = sb


def format_owfs(image, total_blocks, label, files, alloc_super=True):
    """Format `image` (bytearray) as an OWFS volume and add `files`.

    `files` is a list of (name_bytes, data_bytes).
    """
    if total_blocks < SUPER_BLOCK + 1 + 1 + INODE_TABLE_BLOCKS + 1:
        raise ValueError("total_blocks too small for OWFS")
    vol = Volume(image, total_blocks, label)

    # root catalog block (bitmap alloc order: rel 0 first)
    root_block = vol.alloc_block()
    vol.root["blocks"].append(root_block)
    vol.write_root_inode()
    for name, data in files:
        vol.add_file(name, data)
    if alloc_super:
        vol.write_superblock()
    return vol


if __name__ == "__main__":
    import sys

    if len(sys.argv) < 4:
        print("usage: owfs_mkfs.py <image> <total_blocks> <file> [file ...]")
        sys.exit(1)
    path, blocks = sys.argv[1], int(sys.argv[2])
    files = []
    for f in sys.argv[3:]:
        with open(f, "rb") as fh:
            files.append((f.encode("utf-8"), fh.read()))
    image = bytearray(BLOCK_START + blocks * BLOCK)
    format_owfs(image, blocks, "MBL", files)
    with open(path, "wb") as fh:
        fh.write(image)
    print("formatted %s (%d blocks, %d files)" % (path, blocks, len(files)))
