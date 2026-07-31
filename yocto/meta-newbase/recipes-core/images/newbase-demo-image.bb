SUMMARY = "Minimal image that boots straight into the newbase demo on KMS/DRM"
LICENSE = "MIT"

inherit core-image

IMAGE_FEATURES += "ssh-server-dropbear"

CORE_IMAGE_EXTRA_INSTALL += " \
    newbase-demo \
    mesa-vulkan-drivers \
    vulkan-tools \
    kernel-modules \
    sdcard-rootfs-resize \
"

# sdcard-rootfs-resize grows the partition/filesystem to fill the actual
# card on first boot, so this only needs to cover build-time headroom
# (package growth, /var, /tmp) before that runs -- not "room to grow" in
# general, which used to justify a much larger static buffer here.
IMAGE_ROOTFS_EXTRA_SPACE = "131072"
