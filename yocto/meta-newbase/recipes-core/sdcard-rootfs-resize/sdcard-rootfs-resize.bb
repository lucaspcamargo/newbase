SUMMARY = "Grow the root partition/filesystem to fill the SD card/eMMC on first boot"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = " \
    file://newbase-rootfs-grow.sh \
    file://newbase-rootfs-grow.service \
"

inherit systemd

# sfdisk/partx (util-linux) grow the partition; resize2fs (e2fsprogs) grows
# the ext4 filesystem to match. See files/newbase-rootfs-grow.sh for why
# systemd-repart/systemd-growfs aren't used directly (MBR, not GPT).
RDEPENDS:${PN} += "util-linux e2fsprogs-resize2fs"

do_install() {
    install -d ${D}${sbindir}
    install -m 0755 ${UNPACKDIR}/newbase-rootfs-grow.sh ${D}${sbindir}/newbase-rootfs-grow.sh

    install -d ${D}${systemd_unitdir}/system
    install -m 0644 ${UNPACKDIR}/newbase-rootfs-grow.service ${D}${systemd_unitdir}/system/newbase-rootfs-grow.service
}

FILES:${PN} += "${systemd_unitdir}/system/newbase-rootfs-grow.service"

SYSTEMD_SERVICE:${PN} = "newbase-rootfs-grow.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"
