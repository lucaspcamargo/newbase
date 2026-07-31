# Building a Raspberry Pi 5 image with Yocto

This produces a minimal image (`newbase-demo-image`) that boots straight to
the `newbase_demo` binary on raw KMS/DRM — no X11, no Wayland compositor.

## Layout

- `meta-newbase/` — the BitBake layer: recipe for the demo (`recipes-newbase/newbase-demo`)
  and the image (`recipes-core/images/newbase-demo-image.bb`).
- `kas/newbase-rpi5.yml` — a [kas](https://kas.readthedocs.io/) project file that
  pins poky + meta-openembedded + meta-raspberrypi + this layer together, so
  you don't hand-maintain `bblayers.conf`/`local.conf`.

## One-time prerequisites

Install [kas](https://kas.readthedocs.io/en/latest/userguide/install.html)
and Docker (or Podman) for `kas-container`, which sandboxes the whole Yocto
toolchain so you don't need to install bitbake's host dependencies yourself.
Grab the `kas-container` wrapper script itself from
https://github.com/siemens/kas/blob/master/kas-container and put it on your
`PATH`.

The demo's CMake configure step runs Python codegen (RTTI glue, resource
indexing) that needs `scripts/requirements.txt`, same as the native Linux
build. This venv must be built **inside** the kas-container image, not on
the host — `kas-container` bind-mounts this repo at `/repo` inside the
container regardless of where it lives on the host, but a host-built venv's
binaries/shebangs won't reliably run against the container's own libc/
python. Build it once as `venv-kas` (not `venv`, so it doesn't collide with
a host-side venv you may use for native builds — `/repo` is the same
bind-mounted checkout, not a copy):

```
kas-container shell yocto/kas/newbase-rpi5.yml -c "
  python3 -m venv --without-pip /repo/venv-kas &&
  curl -sS https://bootstrap.pypa.io/get-pip.py -o /tmp/get-pip.py &&
  /repo/venv-kas/bin/python /tmp/get-pip.py --quiet &&
  /repo/venv-kas/bin/pip install --quiet -r /repo/scripts/requirements.txt
"
```

(The kas-container image doesn't ship `ensurepip`/`python3-venv`, and since
the container runs with `--rm`, an `apt install` wouldn't persist across
invocations anyway — bootstrapping pip manually via `get-pip.py` sidesteps
both problems and needs no root. Only the venv *files* need to survive,
and they do, since `/repo` is a bind mount of the host checkout.)

The kas file already points `PYTHON_INTERPRETER` at
`/repo/venv-kas/bin/python` via `export` in `local_conf_header`, so nothing
else is needed once the venv exists.

`kas` checks out poky/meta-openembedded/meta-raspberrypi and writes
`build/`, `downloads/`, and `sstate-cache/` under `KAS_WORK_DIR`, which
defaults to wherever you invoke `kas-container` from — i.e. straight into
this source tree if left alone. We keep `KAS_WORK_DIR` at that default
(the `meta-newbase` self-repo's `path:` in the kas file is resolved
relative to it, so moving it would break that), but redirect the three
fetched layers via explicit `path:` entries in the kas file, and redirect
the build/download/sstate dirs via env vars in the build command below —
all landing under `build/yocto/` (already covered by the root `build`
entry in `.gitignore`).

## Building

From the repo root:

```
KAS_BUILD_DIR="$(pwd)/build/yocto/build" \
DL_DIR="$(pwd)/build/yocto/downloads" \
SSTATE_DIR="$(pwd)/build/yocto/sstate-cache" \
kas-container build yocto/kas/newbase-rpi5.yml
```

The resulting image lands under
`build/yocto/build/tmp/deploy/images/raspberrypi5/`. Flash
`newbase-demo-image-raspberrypi5.rootfs.wic.bz2` to an SD card/eMMC with
`bmaptool` or `dd`, same as any other Raspberry Pi Yocto image.

First build will take a while (it's building a full toolchain + Mesa +
kernel). Expect to iterate on the recipe — see "Known rough edges" below.

## Iterating on the source (committed/pushed only)

`newbase-demo_git.bb` fetches the demo source (and its `vendored/`
submodules) via `gitsm` from the project's own remote
(`NEWBASE_SRC_URI`/`SRCREV`, `gt.camargo.eng.br`), so it only ever sees
committed, pushed code — uncommitted working-tree edits to the C++/CMake
source or `vendored/` aren't picked up until pushed. (We tried
`externalsrc` to point bitbake straight at this checkout instead, since
`yocto/meta-newbase` and the source it builds are the same monorepo, but
poky's `externalsrc.bbclass` has two rough edges that don't play well with
"build dir lives inside the same checkout it's building": its
`do_compile[file-checksums]` source-tree hashing does a `git add -A .`
dance that misfires because of a git-dir string-comparison bug, and its
`do_configure` prefunc unconditionally tries to drop symlinks inside `S`,
which fails outright since `kas-container` bind-mounts the repo read-only.
Reverted in favor of the normal fetch + patches below.)

The layer itself (`yocto/meta-newbase`, including any `.patch` files in a
recipe's `files/`) is always read straight from this local checkout via the
kas file's local `path:` entry — those never need to be pushed to take
effect, only committed if you want them kept around (see
`newbase-demo/files/0001-kmsdrm-debug-logging.patch` for an example of
patching a `vendored/` submodule this way, applied via the normal
`SRC_URI += "file://*.patch"` + `do_patch` mechanism).

## What you need to know about Yocto layers

A **layer** is just a directory with a `conf/layer.conf` plus
`recipes-*/*/*.bb` files — there's no other magic to it. BitBake doesn't
"install" layers; you list the ones you want in `bblayers.conf` (or, here,
kas does that for you from the `repos:` section), and it scans all of them
for recipes and config snippets. A few things worth knowing before poking at
this further:

- **Recipes (`.bb`) build one package.** `newbase-demo_git.bb` describes how
  to fetch, configure (`cmake`), compile, and install one thing — the
  underscore-`git` in the filename means "version tracks a git `SRCREV`".
  `.bbappend` files (none here yet) let *other* layers patch an existing
  recipe without forking it — e.g. meta-raspberrypi could ship a
  `newbase-demo_%.bbappend` to tweak our recipe without touching this layer.
- **Machine config comes from meta-raspberrypi**, not from us. Setting
  `MACHINE = "raspberrypi5"` (in the kas file) pulls in its
  `conf/machine/raspberrypi5.conf`, which is what actually turns on the
  `vc4-kms-v3d` DT overlay, kernel fragments, and boot firmware layout. Our
  layer only adds *our* recipes on top; it doesn't redefine the machine.
- **Image recipes (`recipes-core/images/*.bb`) are just recipes whose output
  is a root filesystem.** `newbase-demo-image.bb` lists what should be
  installed (`CORE_IMAGE_EXTRA_INSTALL`) — including our own `newbase-demo`
  package — and `inherit core-image` does the rootfs assembly.
- **Layer priority and `LAYERSERIES_COMPAT`** (both in `conf/layer.conf`)
  exist to catch mismatches early: priority resolves which layer "wins" if
  two define the same recipe, and the compat string is BitBake refusing to
  even parse a layer against a Yocto release it wasn't tested against. Ours
  claims `walnascar scarthgap` — the two most recent release
  lines — since this layer is genuinely simple enough to not care which.
- **kas is not a Yocto concept, it's a wrapper.** Plain Yocto has no
  standard way to declare "these layer repos, at these revisions, with this
  MACHINE/DISTRO" — you'd normally do it by hand with `bitbake-layers
  add-layer` and editing `local.conf`. `kas/newbase-rpi5.yml` just captures
  all of that in one reviewable, versioned file.

## Known rough edges / things to verify on first real build

- **Vulkan (v3dv) on RPi5**: mature since Mesa 24.3 (Vulkan 1.3 conformant),
  which any Yocto release from `scarthgap` onward should carry. If
  `DISTRO_FEATURES:append = " vulkan"` doesn't get mesa to build the
  `broadcom` (v3dv) Vulkan driver on your pinned release, you may need an
  explicit `PACKAGECONFIG:append:pn-mesa = " vulkan"` in the kas
  `local_conf_header` — check `bitbake -e mesa | grep ^PACKAGECONFIG=`.
- **DEPENDS on the recipe** (`libdrm virtual/mesa vulkan-loader udev
  alsa-lib`) cover what SDL3's `kmsdrm`/Vulkan backends need at
  configure/link time — expand this list if CMake's `find_package`/
  pkg-config checks fail for something else (SDL has a lot of optional
  backends it'll silently skip if a dep is missing, so a failed configure
  is more likely than a silent misconfiguration).
- **The systemd unit runs as root** on `tty1` for simplicity (needs raw DRM
  master + evdev access). Worth revisiting once boot is reliable — drop
  privileges to a user in `video`/`render`/`input` groups with the
  appropriate udev `TAG+="uaccess"` rules.
- This has not yet been build-tested end to end — treat the first `kas
  build` as the actual validation step, especially the CMake
  configure/codegen path under cross-compilation.
