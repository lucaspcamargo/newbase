#!/bin/sh
# Grows the root partition to fill the rest of its backing block device (SD
# card/eMMC), then grows the ext4 filesystem to match. Runs once, guarded by
# MARKER below -- safe to leave enabled permanently.
#
# meta-raspberrypi's default .wks uses an MBR/DOS partition table, not GPT,
# so systemd-repart (GPT-only) can't be used to grow the partition itself;
# sfdisk's "resize last partition" idiom works with both table types.

set -e

MARKER=/etc/newbase-rootfs-grown

if [ -e "$MARKER" ]; then
    exit 0
fi

ROOT_SRC=$(findmnt -n -o SOURCE /)
ROOT_PART_NUM=$(echo "$ROOT_SRC" | grep -o '[0-9]*$')
DISK=$(lsblk -no PKNAME "$ROOT_SRC" | head -n1)
DISK="/dev/${DISK}"

if [ -z "$ROOT_PART_NUM" ] || [ ! -b "$DISK" ]; then
    echo "newbase-rootfs-grow: could not determine root disk/partition from $ROOT_SRC, skipping" >&2
    touch "$MARKER"
    exit 0
fi

# Grow the partition table entry to use all remaining space on the disk.
echo ", +" | sfdisk --no-reread -N "$ROOT_PART_NUM" "$DISK" || {
    echo "newbase-rootfs-grow: sfdisk grow failed, skipping" >&2
    touch "$MARKER"
    exit 0
}

# Make the kernel aware of the new partition size without needing a reboot
# (blockdev --rereadpt can fail on a disk with in-use partitions; partx -u
# is designed to update in-use partition tables).
partx -u "$DISK" || true

# Grow the filesystem to fill the now-larger partition. resize2fs supports
# growing a mounted ext4 filesystem online.
resize2fs "$ROOT_SRC"

touch "$MARKER"
