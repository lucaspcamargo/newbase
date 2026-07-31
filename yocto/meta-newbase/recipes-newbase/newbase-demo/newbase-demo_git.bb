SUMMARY = "newbase engine demo application, built for KMS/DRM targets"
HOMEPAGE = "https://gt.camargo.eng.br/camargo/newbase"
LICENSE = "BSD-3-Clause"
LIC_FILES_CHKSUM = "file://LICENSE;md5=3c671ec84ea82b508bda2c5c5cf0d93b"

# Points at the same repo/branch this layer ships in. Override NEWBASE_SRC_URI
# / SRCREV from local.conf (or a bbappend) to build a pinned tag/commit instead
# of tracking main.
NEWBASE_SRC_URI ?= "gitsm://gt.camargo.eng.br/camargo/newbase.git;protocol=https;branch=main"
SRC_URI = "${NEWBASE_SRC_URI}"
SRCREV = "${AUTOREV}"
# Recent poky no longer auto-computes this (bitbake.conf just sets SRCPV =
# "" unconditionally) -- opt in explicitly so PV actually embeds a revision.
SRCPV = "${@bb.fetch2.get_srcrev(d)}"
PV = "0.1.0+git${SRCPV}"

# cmake/NewbaseLua.cmake uses CMake's FetchContent to grab Lua at configure
# time, which doesn't work under bitbake: do_configure runs network-sandboxed
# (FETCHCONTENT_FULLY_DISCONNECTED=ON, set by cmake.bbclass). Fetch the same
# tarball/checksum through bitbake's own fetcher instead (do_fetch does have
# network access) and hand its extracted path to NEWBASE_LUA_ROOT, the escape
# hatch NewbaseLua.cmake already provides for exactly this case.
SRC_URI += "https://www.lua.org/ftp/lua-5.5.0.tar.gz;name=lua"
SRC_URI[lua.sha256sum] = "57ccc32bbbd005cab75bcc52444052535af691789dba2b9016d5c50640d68b3d"

# TEMPORARY -- RPi5 kmsdrm bring-up debugging only, drop once the runtime
# "kmsdrm not available" issue is diagnosed. See the patch header for what
# it does. Applies against the vendored/SDL submodule fetched as part of
# this same gitsm checkout.
SRC_URI += "file://0001-kmsdrm-debug-logging.patch"

# DEBUG_PREFIX_MAP (bitbake.conf) only strips ${S}/${B}/staging-dir prefixes
# from compiled debug info -- Lua's sources live under ${UNPACKDIR} instead
# (outside both), so its .c files' DWARF info would otherwise embed the raw
# TMPDIR-rooted build path, failing the do_package_qa "buildpaths" check.
DEBUG_PREFIX_MAP:append = " -fdebug-prefix-map=${UNPACKDIR}= -fmacro-prefix-map=${UNPACKDIR}="

S = "${WORKDIR}/git"

inherit cmake pkgconfig systemd

# The demo links Python-generated code (RTTI glue, resource index) produced at
# CMake configure time by scripts/*.py, which need scripts/requirements.txt
# (jinja2, PyYAML, tree-sitter, tree-sitter-c). Rather than packaging those as
# bitbake python3-*-native recipes, reuse the venv workflow the project's
# native Linux/Emscripten builds already use: prepare it once outside bitbake
# and pass it through via the PYTHON_INTERPRETER env var (see
# cmake/NewbasePythonInterp.cmake and yocto/README.md for the passthrough
# setup). python3-native is still depended on as a fallback interpreter.
DEPENDS += "python3-native"

# Target-side libs the vendored SDL3 build links/dlopens for the kmsdrm video
# driver + SDL_GPU's Vulkan backend. mesa (v3d GL / v3dv Vulkan ICD) is
# provided by the raspberrypi5 machine's default MACHINE_FEATURES; make sure
# it isn't disabled (DISABLE_VC4GRAPHICS != "1").
DEPENDS += "libdrm virtual/mesa vulkan-loader udev alsa-lib"

# DEPENDS only stages libs into the build-time sysroot; SDL dlopens these
# by SONAME at runtime, so each also needs an explicit RDEPENDS or the
# target image just doesn't have it:
#   mesa-vulkan-drivers -- Vulkan ICD (libvulkan_broadcom.so + icd.d json)
#   libgbm              -- GBM, used by the kmsdrm video driver
#   libegl-mesa         -- EGL, required by kmsdrm to create a GL context
#   mesa-megadriver     -- v3d Gallium/DRI driver .so that libEGL/libgbm load
#   libgl-mesa          -- desktop GL, SDL's first EGL/GL load attempt
#   libgles2-mesa       -- GLES2, SDL's fallback EGL/GL load attempt
#   vulkan-loader       -- the actual Vulkan loader (ships as libvulkan1;
#                          depend on this name, not "libvulkan1" -- that's
#                          a debian.bbclass rename bitbake can't see yet)
# Input (evdev + libudev.so.1) and audio (ALSA/libasound.so.2) needed no
# equivalent fix -- both already end up on the image transitively.
RDEPENDS:${PN} += "mesa-vulkan-drivers libgbm libegl-mesa mesa-megadriver libgl-mesa libgles2-mesa vulkan-loader"

EXTRA_OECMAKE += " \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DNEWBASE_LTO=ON \
    -DSDL_KMSDRM=ON \
    -DSDL_X11=OFF \
    -DSDL_WAYLAND=OFF \
    -DSDL_VULKAN=ON \
    -DSDL_UNIX_CONSOLE_BUILD=ON \
    -DNEWBASE_LUA_ROOT=${UNPACKDIR}/lua-5.5.0 \
"

# The top-level CMakeLists also configures runner/ and a dpi_test dev
# executable alongside the demo; neither defines install() rules so they
# build but aren't packaged. Only newbase_demo (bin/) and its resources
# (share/newbase_demo/) end up in the image.
FILES:${PN} += "${datadir}/newbase_demo ${datadir}/icons"

do_install:append() {
    install -d ${D}${systemd_unitdir}/system
    install -m 0644 ${THISDIR}/files/newbase-demo.service ${D}${systemd_unitdir}/system/newbase-demo.service
}

SYSTEMD_SERVICE:${PN} = "newbase-demo.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

FILES:${PN} += "${systemd_unitdir}/system/newbase-demo.service"
