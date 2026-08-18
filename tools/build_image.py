#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""build_image.py - assemble the UEFI test disk image.

Layout:
    0x000000 - 0x0001FF  Protective MBR (LBA 0)
    0x000200 - 0x0003FF  GPT header (LBA 1)
    0x000400 - 0x000FFF  GPT partition entries (LBA 2..5, 128 entries)
    0x010000 - 0x08FFFF  FAT32 ESP (64 MiB, starts at LBA 128)
                           Contains \EFI\BOOT\BOOTX64.EFI
    0x090000 - ...       OWFS data partition (starts at LBA 1152)
                           Contains the test kernel as "kernel.bin"

The GPT disk GUID is randomly generated per-build.  Partition GUIDs are
fixed well-known values for reproducibility.

Uses only stdlib (no external dependencies) + owfs_mkfs.py for formatting.
"""

import os
import struct
import subprocess
import sys
import uuid

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, "tools")
BUILD = os.path.join(ROOT, "build")

sys.path.insert(0, TOOLS)
from owfs_mkfs import format_owfs

# ---------------------------------------------------------------------------
# Layout constants
# ---------------------------------------------------------------------------
SECTOR       = 512
ESP_LBA      = 128          # ESP starts at LBA 128 (offset 0x10000)
ESP_SIZE_MB  = 64
ESP_SECTORS  = (ESP_SIZE_MB * 1024 * 1024) // SECTOR   # 131072
OWFS_LBA     = ESP_LBA + ESP_SECTORS                    # 131200
OWFS_SIZE_MB = 32
OWFS_SECTORS = (OWFS_SIZE_MB * 1024 * 1024) // SECTOR  # 65536
OWFS_TOTAL_BLOCKS = 2048

# Disk size: OWFS_LBA + OWFS_SECTORS sectors, rounded up to 1 MiB
DISK_SIZE_MB = ((OWFS_LBA + OWFS_SECTORS) * SECTOR + 0xFFFFF) // 0x100000
DISK_SECTORS = DISK_SIZE_MB * 1024 * 1024 // SECTOR
DISK_BYTES   = DISK_SECTORS * SECTOR

# GUIDs (random per build)
DISK_GUID = uuid.uuid4()

# Well-known partition type GUIDs
ESP_TYPE_GUID  = uuid.UUID("C12A7328-F81F-11D2-BA4B-00A0C93EC93B")
OWFS_TYPE_GUID = uuid.UUID("E6D6D379-F507-44C2-A23C-238F2A3DF928")  # Linux data

# Fixed partition GUIDs (just random but constant)
ESP_PART_GUID  = uuid.UUID("12345678-1234-1234-1234-123456789ABC")
OWFS_PART_GUID = uuid.UUID("ABCDEF01-2345-6789-ABCD-EF0123456789")

OUT_IMAGE = os.path.join(ROOT, "mbl_test.img")


def crc32(data):
    """Standard CRC32 (used for GPT header/partition entries)."""
    import binascii
    return binascii.crc32(data) & 0xFFFFFFFF


def run(cmd):
    print("+ " + " ".join(cmd))
    subprocess.run(cmd, check=True)


# ---------------------------------------------------------------------------
# MBR (protective)
# ---------------------------------------------------------------------------
def write_protective_mbr(image):
    """Write a protective MBR at LBA 0."""
    # Partition entry 1: type 0xEE, covers the whole disk
    image[446 + 0] = 0x00          # status
    image[446 + 1] = 0x00          # CHS first (unused for GPT)
    image[446 + 2] = 0x01
    image[446 + 3] = 0x00
    image[446 + 4] = 0xEE          # type: GPT protective
    image[446 + 5] = 0xFF          # CHS last
    image[446 + 6] = 0xFF
    image[446 + 7] = 0xFF
    # Start LBA and size (little-endian 32-bit)
    struct.pack_into("<I", image, 446 + 8, 1)
    struct.pack_into("<I", image, 446 + 12, DISK_SECTORS - 1 if DISK_SECTORS <= 0xFFFFFFFF else 0xFFFFFFFF)

    # Boot signature
    struct.pack_into("<H", image, 510, 0xAA55)


def write_gpt(image):
    """Write GPT headers and partition tables (both primary and backup)."""
    # 1. Partition entries (128 entries of 128 bytes = 16384 bytes = 32 sectors)
    entries = bytearray(128 * 128)

    def pack_entry(buf, idx, type_guid, part_guid, first_lba, last_lba, name):
        eoff = idx * 128
        buf[eoff:eoff + 16] = type_guid.bytes_le
        buf[eoff + 16:eoff + 32] = part_guid.bytes_le
        struct.pack_into("<Q", buf, eoff + 32, first_lba)
        struct.pack_into("<Q", buf, eoff + 40, last_lba)
        struct.pack_into("<Q", buf, eoff + 48, 0)  # attributes
        name_utf16 = name.encode("utf-16-le")[:72]
        buf[eoff + 56:eoff + 56 + len(name_utf16)] = name_utf16

    # Partition 1: ESP (64MB)
    pack_entry(entries, 0, ESP_TYPE_GUID, ESP_PART_GUID,
               ESP_LBA, ESP_LBA + ESP_SECTORS - 1, "EFI System Partition")
    # Partition 2: OWFS data (32MB)
    pack_entry(entries, 1, OWFS_TYPE_GUID, OWFS_PART_GUID,
               OWFS_LBA, OWFS_LBA + OWFS_SECTORS - 1, "MBL OWFS Data")

    entries_crc = crc32(bytes(entries))

    # Primary partition table at LBA 2
    image[2 * SECTOR:2 * SECTOR + len(entries)] = entries

    # Backup partition table at LBA (DISK_SECTORS - 33)
    backup_entries_lba = DISK_SECTORS - 33
    image[backup_entries_lba * SECTOR:backup_entries_lba * SECTOR + len(entries)] = entries

    # Primary GPT Header at LBA 1
    pri_hdr = bytearray(SECTOR)
    struct.pack_into("<8s", pri_hdr, 0, b"EFI PART")
    struct.pack_into("<I", pri_hdr, 8, 0x00010000)
    struct.pack_into("<I", pri_hdr, 12, 92)
    struct.pack_into("<I", pri_hdr, 16, 0)
    struct.pack_into("<I", pri_hdr, 20, 0)
    struct.pack_into("<Q", pri_hdr, 24, 1)                      # MyLBA
    struct.pack_into("<Q", pri_hdr, 32, DISK_SECTORS - 1)       # AlternateLBA
    struct.pack_into("<Q", pri_hdr, 40, ESP_LBA)                # FirstUsableLBA
    struct.pack_into("<Q", pri_hdr, 48, DISK_SECTORS - 34)      # LastUsableLBA
    struct.pack_into("<16s", pri_hdr, 56, DISK_GUID.bytes_le)
    struct.pack_into("<Q", pri_hdr, 72, 2)                      # PartitionEntryLBA
    struct.pack_into("<I", pri_hdr, 80, 128)
    struct.pack_into("<I", pri_hdr, 84, 128)
    struct.pack_into("<I", pri_hdr, 88, entries_crc)
    struct.pack_into("<I", pri_hdr, 16, crc32(bytes(pri_hdr[:92])))
    image[1 * SECTOR:2 * SECTOR] = pri_hdr

    # Backup GPT Header at LBA DISK_SECTORS - 1
    bak_hdr = bytearray(SECTOR)
    struct.pack_into("<8s", bak_hdr, 0, b"EFI PART")
    struct.pack_into("<I", bak_hdr, 8, 0x00010000)
    struct.pack_into("<I", bak_hdr, 12, 92)
    struct.pack_into("<I", bak_hdr, 16, 0)
    struct.pack_into("<I", bak_hdr, 20, 0)
    struct.pack_into("<Q", bak_hdr, 24, DISK_SECTORS - 1)       # MyLBA
    struct.pack_into("<Q", bak_hdr, 32, 1)                      # AlternateLBA
    struct.pack_into("<Q", bak_hdr, 40, ESP_LBA)                # FirstUsableLBA
    struct.pack_into("<Q", bak_hdr, 48, DISK_SECTORS - 34)      # LastUsableLBA
    struct.pack_into("<16s", bak_hdr, 56, DISK_GUID.bytes_le)
    struct.pack_into("<Q", bak_hdr, 72, backup_entries_lba)     # PartitionEntryLBA
    struct.pack_into("<I", bak_hdr, 80, 128)
    struct.pack_into("<I", bak_hdr, 84, 128)
    struct.pack_into("<I", bak_hdr, 88, entries_crc)
    struct.pack_into("<I", bak_hdr, 16, crc32(bytes(bak_hdr[:92])))
    image[(DISK_SECTORS - 1) * SECTOR:DISK_SECTORS * SECTOR] = bak_hdr


# ---------------------------------------------------------------------------
# FAT32 ESP
# ---------------------------------------------------------------------------
def write_fat32_esp(image, efi_data):
    """Write a minimal compliant FAT32 filesystem into the ESP region.

    Creates \\EFI\\BOOT\\BOOTX64.EFI.
    """
    base = ESP_LBA * SECTOR
    total_sectors = ESP_SECTORS
    sectors_per_cluster = 1  # 512-byte clusters -> ~128K clusters (>65525 -> FAT32)
    reserved_sectors = 32    # standard FAT32 reserved
    num_fats = 2
    root_cluster = 2
    hidden_sectors = ESP_LBA
    fat_size = 1024  # 1024 sectors = 512 KiB covers 131072 clusters

    # BPB (BIOS Parameter Block)
    bpb = bytearray(512)
    bpb[0:3] = b'\xEB\x58\x90'                        # jump
    bpb[3:11] = b'MSWIN4.1'                           # OEM
    struct.pack_into("<H", bpb, 11, SECTOR)          # bytes per sector
    bpb[13] = sectors_per_cluster
    struct.pack_into("<H", bpb, 14, reserved_sectors)
    bpb[16] = num_fats
    struct.pack_into("<H", bpb, 17, 0)               # root entry count (0 for FAT32)
    struct.pack_into("<H", bpb, 19, 0)               # total sectors 16
    bpb[21] = 0xF8                                   # media type
    struct.pack_into("<H", bpb, 22, 0)               # FAT size 16
    struct.pack_into("<H", bpb, 24, 63)              # sectors per track
    struct.pack_into("<H", bpb, 26, 255)             # number of heads
    struct.pack_into("<I", bpb, 28, hidden_sectors)
    struct.pack_into("<I", bpb, 32, total_sectors)

    # FAT32-specific fields
    struct.pack_into("<I", bpb, 36, fat_size)
    struct.pack_into("<H", bpb, 40, 0)               # extended flags
    struct.pack_into("<H", bpb, 42, 0)               # FAT version
    struct.pack_into("<I", bpb, 44, root_cluster)
    struct.pack_into("<H", bpb, 48, 1)               # FSInfo sector
    struct.pack_into("<H", bpb, 50, 6)               # backup boot sector
    bpb[64] = 0x80                                   # drive number
    bpb[65] = 0                                      # reserved
    bpb[66] = 0x29                                   # extended boot sig
    struct.pack_into("<I", bpb, 67, 0x12345678)      # volume serial
    bpb[71:82] = b'MBL BOOT   '                     # volume label
    bpb[82:90] = b'FAT32   '
    struct.pack_into("<H", bpb, 510, 0xAA55)         # Boot signature

    # Write BPB (sector 0) and Backup BPB (sector 6)
    image[base:base + 512] = bpb
    image[base + 6 * SECTOR:base + 7 * SECTOR] = bpb

    # FSInfo sector (sector 1) and Backup FSInfo (sector 7)
    fsinfo = bytearray(512)
    struct.pack_into("<I", fsinfo, 0, 0x41615252)    # LeadSig
    struct.pack_into("<I", fsinfo, 484, 0x61417272)  # StrucSig
    struct.pack_into("<I", fsinfo, 488, 0xFFFFFFFF)  # FreeCount
    struct.pack_into("<I", fsinfo, 492, 0xFFFFFFFF)  # NxtFree
    struct.pack_into("<I", fsinfo, 508, 0xAA550000)  # TrailSig
    image[base + 1 * SECTOR:base + 2 * SECTOR] = fsinfo
    image[base + 7 * SECTOR:base + 8 * SECTOR] = fsinfo

    # FAT tables
    fat = bytearray(fat_size * SECTOR)
    struct.pack_into("<I", fat, 0, 0x0FFFFFF8)       # media type
    struct.pack_into("<I", fat, 4, 0x0FFFFFFF)       # end-of-chain
    struct.pack_into("<I", fat, 8, 0x0FFFFFFF)       # cluster 2: root directory (EOC)
    struct.pack_into("<I", fat, 12, 0x0FFFFFFF)      # cluster 3: \EFI directory (EOC)
    struct.pack_into("<I", fat, 16, 0x0FFFFFFF)      # cluster 4: \EFI\BOOT directory (EOC)

    # Data region starts after reserved sectors + all FATs
    cluster_size = sectors_per_cluster * SECTOR
    data_start = base + (reserved_sectors + num_fats * fat_size) * SECTOR

    def get_cluster_offset(c):
        return data_start + (c - 2) * cluster_size

    # Cluster 2: Root Directory
    root = bytearray(cluster_size)
    root[0:11] = b"EFI        "
    root[11] = 0x10                                  # directory attribute
    struct.pack_into("<H", root, 20, 0)              # first cluster high
    struct.pack_into("<H", root, 26, 3)              # first cluster low = 3
    struct.pack_into("<I", root, 28, 0)              # size
    image[get_cluster_offset(2):get_cluster_offset(2) + cluster_size] = root

    # Cluster 3: \EFI Directory
    efi_dir = bytearray(cluster_size)
    # . entry
    efi_dir[0:11] = b".          "
    efi_dir[11] = 0x10
    struct.pack_into("<H", efi_dir, 20, 0)
    struct.pack_into("<H", efi_dir, 26, 3)
    # .. entry
    efi_dir[32:43] = b"..         "
    efi_dir[43] = 0x10
    struct.pack_into("<H", efi_dir, 52, 0)
    struct.pack_into("<H", efi_dir, 58, 0)
    # BOOT entry
    efi_dir[64:75] = b"BOOT       "
    efi_dir[75] = 0x10
    struct.pack_into("<H", efi_dir, 84, 0)
    struct.pack_into("<H", efi_dir, 90, 4)
    image[get_cluster_offset(3):get_cluster_offset(3) + cluster_size] = efi_dir

    # Cluster 4: \EFI\BOOT Directory
    boot_dir = bytearray(cluster_size)
    # . entry
    boot_dir[0:11] = b".          "
    boot_dir[11] = 0x10
    struct.pack_into("<H", boot_dir, 20, 0)
    struct.pack_into("<H", boot_dir, 26, 4)
    # .. entry
    boot_dir[32:43] = b"..         "
    boot_dir[43] = 0x10
    struct.pack_into("<H", boot_dir, 52, 0)
    struct.pack_into("<H", boot_dir, 58, 3)
    # BOOTX64.EFI file entry
    boot_dir[64:75] = b"BOOTX64 EFI"
    boot_dir[75] = 0x20                              # archive attribute
    struct.pack_into("<H", boot_dir, 84, 0)          # cluster high
    struct.pack_into("<H", boot_dir, 90, 5)          # cluster low = 5
    struct.pack_into("<I", boot_dir, 92, len(efi_data))
    image[get_cluster_offset(4):get_cluster_offset(4) + cluster_size] = boot_dir

    # Clusters 5+: BOOTX64.EFI binary
    cluster = 5
    remaining = efi_data
    while remaining:
        chunk = remaining[:cluster_size]
        remaining = remaining[cluster_size:]
        image[get_cluster_offset(cluster):get_cluster_offset(cluster) + len(chunk)] = chunk
        if remaining:
            struct.pack_into("<I", fat, cluster * 4, cluster + 1)
        else:
            struct.pack_into("<I", fat, cluster * 4, 0x0FFFFFFF)
        cluster += 1

    # Write FAT 1 and FAT 2
    fat1_off = base + reserved_sectors * SECTOR
    fat2_off = fat1_off + fat_size * SECTOR
    image[fat1_off:fat1_off + len(fat)] = fat
    image[fat2_off:fat2_off + len(fat)] = fat


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    # 1. Build the EFI application
    subprocess.run([sys.executable, os.path.join(TOOLS, "build_efi.py")], check=True)

    # 2. Assemble the test kernel (32-bit flat binary for the kernel payload)
    nasm = os.environ.get("MBL_NASM", r"C:\Program Files\NASM\nasm.exe")
    subprocess.run([nasm, "-f", "bin",
                    os.path.join(ROOT, "boot", "test_kernel.asm"),
                    "-o", os.path.join(BUILD, "test_kernel.bin")], check=True)

    # 3. Read outputs
    with open(os.path.join(BUILD, "BOOTX64.EFI"), "rb") as f:
        efi_data = f.read()
    with open(os.path.join(BUILD, "test_kernel.bin"), "rb") as f:
        kernel = f.read()

    print("  BOOTX64.EFI: %d bytes" % len(efi_data))
    print("  test_kernel.bin: %d bytes" % len(kernel))

    # 4. Create disk image
    image = bytearray(DISK_BYTES)

    write_protective_mbr(image)
    write_gpt(image)
    write_fat32_esp(image, efi_data)

    # 5. Format OWFS partition (starts at OWFS_LBA * SECTOR)
    format_owfs(image, OWFS_TOTAL_BLOCKS, "MBL", [(b"kernel.bin", kernel)],
                block_start=OWFS_LBA * SECTOR)

    # 6. Write output
    with open(OUT_IMAGE, "wb") as f:
        f.write(image)

    print("wrote %s (%d bytes, %d MiB)" % (OUT_IMAGE, len(image), len(image) // 0x100000))
    return 0


if __name__ == "__main__":
    sys.exit(main())
