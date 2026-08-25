# `lupi` System

`lupi` is a compatibility layer for games written in Lua against the
[Lupi console API](https://lupi.api.br/docs/), a 480x270, 256-color indexed
(BGR555 palette) retro console driven by a single `update(frame)` callback per
frame. It plays the same role `src/sgdk` does for real SGDK/Mega Drive C
games, but since Lupi carts are pure Lua with one non-blocking per-frame
callback (no translated foreign C code, no hardware to emulate), `lupi` runs
synchronously on the host thread, no separate game thread or semaphores.

Each cart gets its own, fully isolated `lua_State`, separate from the
engine's `script_lua` system and its RTTI-driven bindings. Lupi's API is a
fixed, closed namespace (`update`, `ui.*`, `sfx.*`, bare globals like `LEFT`),
so it's bound with plain C functions rather than the meta_any "box" pattern
`script_lua` uses for reflected engine types.

**Real carts run from source, unmodified, with no Lupi devkit involved.**
Lupi's own build pipeline (a separate tool, not part of this engine) compiles
a cart's source folder into a release with a generated `sprites.lua`,
`palette.lua`, and compiled `maps/*.lua` (from Tiled JSON). We don't have that
compiler, so `lupi` reimplements its job itself, at cart-boot time — see
"Cart layout" and "`require()`" below. This has been verified end-to-end
against two real, complete games ("Balão Gatinho", "Caio Pernocas") running
from their original source trees with zero modifications.

## Reference sources

Two external repos have been used purely as read-only references to verify
this implementation against the real thing — neither is a runtime or build
dependency of `lupi`, both are only ever consulted by reading their source
and manually porting the relevant behavior into our own C++:

- [github.com/lupi-org-br/lupi-codec](https://github.com/lupi-org-br/lupi-codec)
  — the real asset compiler (Lua + ImageMagick) that produces a cart's
  `sprites.lua`/`palette.lua`/compiled maps from source. See "Asset
  auto-discovery" and "`ui.map` data contract" below.
- [github.com/lupi-org-br/lupinho](https://github.com/lupi-org-br/lupinho)
  — **the official Lupi simulator**, a C (raylib + Lua) implementation of
  the actual console runtime — i.e. the thing that actually executes
  `update(frame)` and interprets every `ui.*` call, as opposed to the
  compiler which only prepares assets ahead of time.
  Confirmed two concrete behaviors against its `src/ui.c`/`src/font.h`:
  - **`ui.print`'s font is fixed, not cart-provided.** It's a classic 5x8
    column-major bitmap font (ASCII 32-126), hardcoded in the runtime, not
    loaded from any cart asset — transcribed verbatim into
    `src/lupi/draw.cpp`'s `font_data` table, replacing an earlier
    self-authored 3x5 approximation. Characters outside 32-126 (`\n`
    included) are silently skipped entirely — no glyph, no cursor advance —
    `ui.print` has no multi-line support in the real console. (This also
    means a cart shipping its own `font/*.png` — seen in "Caio Pernocas" — is
    simply unused/dead weight; nothing in the real runtime or compiler ever
    reads a cart-provided font.)
  - **Palette index 0 always renders fully transparent**, regardless of
    whatever color `ui.palset(0, ...)` has set it to — hardcoded in the
    real engine's `get_palette_color()`, not a property of the BGR555
    encoding itself. Replicated in `lupi::step()`'s `RENDER`-phase blit
    (`src/lupi/lupi.cpp`): index 0 always maps to alpha 0, independent of
    `pal.bgr555[0]`'s actual stored value.
  - **`sfx.*` and `ui.layout()`/Clay have no reference implementation at
    all** — neither is registered anywhere in lupinho's source (no `sfx`
    global, no `layout` field, no `Box`/`Text`/`Image`/`Custom`/`Clay`
    anywhere), and neither real cart calls any of them. See "Deferred
    work" below — nothing to catch up to yet on either front.
  - **`fillp`'s dither mask applies to every geometric primitive**
    (lines, rect/circle outlines, not just filled shapes), and a *set* bit
    means draw, not skip — an earlier version of this file had both the
    scope and the polarity backwards. Fixed in `src/lupi/draw.cpp`.
  - **`ui.rect`/`ui.rectfill`'s far corner is exclusive**: they draw a
    `width x height` rect internally (`width = x2-x1`), so an earlier
    version of this file — which treated both corners as inclusive — drew
    one extra pixel along each axis. Also confirmed: no coordinate
    normalization — a "backwards" rect (far corner before near corner)
    draws nothing, it isn't auto-corrected. Fixed in `src/lupi/draw.cpp`.
  - **`ui.spr`/`ui.tile`'s real signatures are simpler than an earlier
    version of this file assumed**: `ui.spr(sheet, x, y, flipped)` takes a
    single optional bool (horizontal flip only — there is no vertical-flip
    parameter for either call), and `ui.tile(sheet, tile_index, x, y)`
    bakes horizontal flip into `tile_index` itself via bit 1024, rather
    than taking separate flip arguments at all. (`ui.map`'s per-cell tile
    ids are the one place both `1024`/`2048` flip bits are honored — see
    "`ui.map` data contract" below — because that data comes from the
    Tiled compiler, a different code path than these two calls.) Confirmed
    against every real call site in both carts, including ones that pass a
    trailing unused `false` — harmless excess Lua args, discarded the same
    way the real engine discards them. Fixed in `src/lupi/api_ui.cpp`.

## Lifecycle

No cart runs by default. `lupi::init()` only sets up the persistent GPU
texture and the F10 debug tool window. Carts are started/stopped at runtime
through RTTI-exposed system functions (see `src/lupi/lupi.cpp`'s
`_rtti_init_lupi`), which `script_lua` turns into plain Lua globals the same
way it does for every other system:

- `lupi_start(cart_path)` — loads and runs `cart_path` (a `lupi.yaml`
  manifest resource path), replacing any cart already running. Also creates
  a `csprite` entity in the default scene showing the live framebuffer.
- `lupi_stop()` — tears down the running cart's `lua_State` and removes its
  scene sprite. Safe to call when nothing is running.
- `lupi_running()` — returns whether a cart is currently running.

`lupi::on_scene_change()` calls `stop()` automatically, so a demo scene only
needs to call `lupi_start(...)` once when entered; leaving the scene stops
the game for you. See `demo/res/lupi_demo/scene.lua` for a minimal example.

The scene sprite is wired up render_simple-specific: it stores the live
texture directly in an `rtexture`/`rsprite` pair and marks it pre-uploaded.
render_gpu, which caches GPU textures separately keyed by `rtexture` identity,
isn't supported by this path.

## Cart layout

A cart is a resource directory containing, exactly as shipped by a real Lupi
project's source repo:

- `game.lua`, the entry script. Must define `update(frame)`, called once per
  simulation frame (`GENERAL_UPDATE` phase) with all game logic and drawing.
  Real Lupi carts always name their entry script exactly `game.lua`; there's
  no way to configure a different name.
- `lupi.yaml`, a small manifest (`name`/`version`/`developer`/`public`).
  Parsed only for logging — real carts declare no asset list or anything else
  `lupi` depends on.
- an `img/` folder (searched recursively) of PNGs — each one becomes a named
  sprite/spritesheet, auto-discovered and auto-palettized at boot (see
  "Asset auto-discovery" below). No manifest entry needed per image.
- a `maps/` folder of Tiled JSON map exports (`*.json`) plus the tileset PNGs
  they reference (also picked up by the same `img/`-style scan since they
  live under the cart directory). Compiled at boot into the same table shape
  Lupi's own compiler would have produced — see "`ui.map` data contract".
- any number of sibling `.lua` files, `require`d from `game.lua` (or from
  each other) by module name with no extension, e.g. `require "player"` →
  `player.lua`. See "`require()`" below for how this is resolved without
  Lua's own file-based `package.path`.

## Asset auto-discovery (`Sprites` / `Palette`)

Real carts ship no asset manifest — the association between a filename and a
`Sprites.find(name)` call only exists implicitly, in the `img/`/`maps/`
folder layout and the game code that references it. `lupi_scan_cart_assets`
(`src/lupi/loaders.cpp`) reimplements the relevant slice of Lupi's own image
compiler (`lupi-codec`'s `image_codec.lua`/`tile_detector.lua`/
`pixel_indexer.lua`) directly in C++, at cart-boot time, before any cart Lua
runs:

1. Every `.png` under the cart directory (via `rman()`'s recursive resource
   index — see `scripts/res_indexer.py`) is **sorted by path first** (`rman()`
   stores resources in an `unordered_map`; iterating it directly makes
   palette allocation order — and therefore which color lands at which
   index — depend on unrelated resources elsewhere in the same table,
   confirmed by a real regression: loading a second, unrelated cart changed
   a first cart's own sprite colors, since it shifted the shared table's
   hash-bucket layout), then decoded and quantized into a single, shared,
   cart-wide 256-entry BGR555 palette (exact-match first,
   then allocate a new slot, then nearest-channel-distance fallback once
   full — the "fallback" is our own addition; the real codec has none and
   simply can't exceed 256 colors in a released cart). Fully transparent
   pixels (`alpha == 0`) map to palette index 0; anything else, including
   partially-transparent pixels, is quantized normally — matches
   `lupi-codec`'s `PixelIndexer` exactly, no alpha-threshold heuristics.
2. **Tile slicing is auto-detected via a magic-marker convention**, not a
   declared tile size: if the image's very top-left pixel is a specific
   mid-gray BGR555 value (`8456`, i.e. r5=g5=b5=8 — see
   `MAGIC_COLOR_BGR555` in `loaders.cpp`), the image is a multi-tile sheet;
   the marker's run-length along the top row/left column gives the tile
   size, and the *entire* first tile row and column are margin guide tiles,
   excluded from the addressable grid (so a 176x176 sheet with a 16x16
   marker yields a 10x10 = 100-tile sheet, addressed from grid position
   (1,1)). If the top-left pixel isn't the marker, the whole image is one
   single-tile "sheet" — used for standalone sprites like animation frames,
   which can be any (even non-square) width/height.
3. Every discovered sheet is exposed to Lua via **two** conventions, since
   different real carts use different ones:
   - `Sprites.find(name)`, keyed by filename stem only (no directory
     prefix) — confirmed against one real cart's call sites
     (`Sprites.find("W1SA")`, `Sprites.find("_hero4")`, etc.), collision-free
     there since its sprite filenames are all distinct.
   - A nested table mirroring the cart's own directory structure, e.g.
     `Sprites.tilemap.clouds` (from `tilemap/clouds.png`),
     `Sprites.poi.cherry["3"]` (from `poi/cherry/3.png`, numeric stems are
     still string keys), `Sprites.player.win.f1` (from
     `player/win/f1.png`) — confirmed against a second real cart that uses
     *only* this form (never `.find()`), needed because its per-folder
     filenames (`f1.png`, `tiles.png`, ...) collide across folders and
     wouldn't be addressable by stem alone.
4. The synthesized `Palette` global (a real cart's `game.lua` does
   `for i=1,#Palette do ui.palset(i-1, Palette[i]) end` every frame) is built
   directly from the palette this same scan just populated — there is no
   real `palette.lua` to reproduce; ours is self-consistent by construction.

## `require()`

Real carts `require` three different kinds of module, none of which exist as
plain sibling `.lua` files as far as the engine can see: `"sprites"` and
`"palette"` are compiler-generated (no source at all), and `"maps.<name>"`
resolves to a Tiled-JSON-compiled table (source is `maps/<name>.json`, not
`.lua`). Rather than trying to satisfy this through Lua's own
`package.path`/`package.preload`, `lupi_register_require`
(`src/lupi/api_require.cpp`) replaces the global `require` outright with a
small C function, cached per-module the same way vanilla `require()` is:

- `"sprites"` / `"palette"` — no-ops; both globals are already populated by
  `lupi_scan_cart_assets` before `game.lua` runs.
- `"maps.<name>"` — looked up in a table of Tiled maps precompiled at boot
  by `lupi_compile_cart_maps` (`src/lupi/tiled_maps.cpp`) from every
  `maps/*.json` under the cart directory. `<name>` can itself be dotted/nested
  (`maps.stages.w1s1` for `maps/stages/w1s1.json`, `maps.world.m` for
  `maps/world/m.json`) — confirmed against a real cart that organizes its
  maps into subfolders; the compiled-map registry key mirrors the file's
  full path relative to the cart root (dots for slashes), not just its
  filename stem.
- anything else — dots are directory separators (`player.player` →
  `player/player.lua`, `player.states.idle` → `player/states/idle.lua` —
  confirmed against a real cart organized this way), resolved as a sibling
  `.lua` resource file, loaded and executed once (real cart modules are
  side-effect-only: they define globals, they don't `return` a table), then
  cached.

## Read-only file access

Inside a Lupi cart, `io.open()` is backed by newbase's resource manager rather
than the host filesystem. It accepts cart-relative paths and read-only modes:

```lua
local file, err = io.open("gfx/tileset.json", "r")
if file then
    local contents = file:read("*a")
    file:close()
end
```

The supported modes are `"r"` and `"rb"`. `file:read("*a")`, `file:read("*l")`,
`file:read("*L")`, and numeric byte counts are supported. Absolute paths,
paths that escape the cart directory, and write modes are rejected.

## Palette overrides

A real cart's own game code routinely hardcodes small "logical" palette
indices directly (e.g. a real cart's `consts.lua` might define `COLORS =
{ LIGHT_BLUE = 18, ... }` and call `ui.cls(COLORS.LIGHT_BLUE)`). Those
numbers only ever made sense against the *original* developer's
`master_palette.json` — accumulated across their entire local build history,
external to the cart's own repo, and permanently unrecoverable from the
cart's own asset content alone (confirmed by direct investigation: even
running the real `lupi-codec` fresh against just a cart's own images
produces different colors than the shipped game, and its `-unique-colors
-depth 5` extraction step applies ImageMagick's default Floyd-Steinberg
dithering, which is position-dependent and impractical to replicate exactly
regardless). Our own auto-scanned palette (`lupi_scan_cart_assets`) is
internally self-consistent — sprites always render correctly, since their
pixel indices and the `Palette` table are quantized together — but has no
way to know what color a specific hardcoded index was *supposed* to be.

`lupi_apply_palette_overrides` (`src/lupi/loaders.cpp`) closes that gap with
an optional, newbase-only YAML sidecar next to a cart's `lupi.yaml` —
**not** part of the real Lupi format, and entirely absent unless someone
has manually confirmed the intended colors for that specific cart (by eye,
from a reference build/screenshot, etc.) and pinned them:

```yaml
# palette_overrides.yaml, sitting next to lupi.yaml
palette:
  18: "#008c8c"  # COLORS.LIGHT_BLUE (sky)
  49: "#b5ce7b"  # COLORS.CLOUDS
```

Applied **before** the normal auto-scan, not after — reserving overridden
indices first means `find_or_allocate_color`'s "first free slot" search
(used both by sprite quantization and `Palette.hex()`) skips them
automatically, so a real sprite color can never land on, and later get
silently overwritten at, an overridden index. (An earlier version of this
applied overrides *after* the scan, which meant they could — and did —
clobber whatever real sprite color the scan happened to allocate there
first; don't reintroduce that ordering.) If an overridden index falls
beyond the auto-scan's contiguous run, the gap is backfilled (as black)
rather than left sparse — real cart code's own
`for i=1,#Palette do ui.palset(i-1, Palette[i]) end` loop needs a
well-defined `#Palette`.

### `Palette.hex(0xRRGGBB)`

A second real cart sidesteps the whole "lost master palette" problem itself,
via its own convention: rather than hardcoding indices, it hardcodes actual
colors and resolves them to an index at load time, e.g.
`kColors.black = Palette.hex(0x000000)`. This is a **real API surface**
(part of `Palette`, alongside the plain array), not lupi-codec output —
implemented as a Lua-callable closure on the `Palette` table (see the end of
`lupi_scan_cart_assets` in `src/lupi/loaders.cpp`) that reuses the exact same
exact-match-then-allocate logic as sprite quantization: given a hex color, it
returns the existing index if that BGR555 value is already in the palette,
otherwise allocates a fresh one — and pushes the new entry into the live Lua
`Palette` array too, so it survives into the cart's own per-frame
`ui.palset` loop. Carts using this convention need no override sidecar;
their colors are correct automatically, every run.

## API surface implemented

`ui.cls/clip/camera/rect/rectfill/draw_rect/circ/circfill/line/trisfill/grid/
spr/tile/map/print/palset/fillp/mid/stat`, `ui.btn/btnp/mouse/peektext/
readtext`, and the button-id/direction constants (`LEFT`, `RIGHT`, `UP`,
`DOWN`, `BTN_Z`, `BTN_X`, `BTN_F`, `BTN_G`, `BTN_Q`, `BTN_E`) — `BTN_X` and
`ui.circ` (plain outline-only circle) have no counterpart in `lupinho` at
all (it only has `circfill` and a generic `draw_circle`, neither of which
we implement `draw_circle` as — not yet added, no real cart has been seen
calling it), but `ui.circ` is kept since one real cart's debug-only code
calls it; harmless either way. `sfx.*` and the Clay-based
`ui.layout()`/`Box()`/`Text()`/`Image()`/`Custom()` builder API are
registered as no-ops for now (see "Deferred" below).

Plain Lua `print(...)` (not `ui.print`) is overridden to route through
`nb::log::info` prefixed with `[lupi_print]`, rather than the stdlib default
of writing to stdout directly — keeps cart debug prints visible alongside
every other `[lupi]` log line. See `lupi_register_print` in
`src/lupi/api_stubs.cpp`.

All drawing-primitive numeric arguments (coordinates, radii, palette/color
indices) accept **any** Lua number, truncating floats to int rather than
requiring an exact integer representation — confirmed necessary against real
cart code, which routinely passes `math.sin()`/division results straight
into e.g. `ui.circfill(x, y, r, color)`. `luaL_checkinteger` (strict in Lua
5.4+) is the wrong tool for this API; see `checkint`/`optint` in
`src/lupi/api_ui.cpp`.

### Input

Only player 0 is bound to real keys in this MVP; players 1-2 and button ids
6-11 are reserved/unbound.

| Button   | id | Keys        |
|----------|----|-------------|
| LEFT     | 0  | Left / A    |
| RIGHT    | 1  | Right / D   |
| UP       | 2  | Up / W      |
| DOWN     | 3  | Down / S    |
| BTN_Z    | 4  | Z           |
| BTN_X    | 5  | X           |
| BTN_F    | 12 | F           |
| BTN_G    | 13 | G           |
| BTN_Q    | 14 | Q           |
| BTN_E    | 15 | E           |

### `ui.map` data contract

This is the **real, confirmed** compiled shape a Lupi cart's `map.lua` and
game code (`director.lua`, `hud.lua`) actually index — reverse-engineered
from `lupi-codec`'s `tiled_codec.lua`/`tiled_processor.lua`/
`tiled_generator.lua` and cross-checked against real cart code that reads
`map.POI.pois[i]` directly, so it is **not** free to redesign; it's
reproduced verbatim (in C++, from raw Tiled JSON) by
`lupi_compile_cart_maps`:

```lua
-- returned by require("maps.M12_12"), i.e. compiled from maps/M12_12.json
{
    metadata = { width = W, height = H, tile_size = T },
    tilesets = { [tileset_name] = image_stem, ... },  -- e.g. W1SA = "W1SA"
    BG1 = {
        [tileset_name] = { [cell_index] = tile_id, ... },  -- sparse, 1-based, row-major
        metadata = <same table as above>,
        tilesets = <same table as above>,
    },
    BG2 = { ... }, FG1 = { ... }, POI = { ... },
}
```

Key details, all confirmed against real map data:

- **A single layer can span multiple tilesets at once** (e.g. a real cart's
  `FG1` layer draws from both `W1SA` and `W1SB`) — `ui.map()`'s C++
  implementation (`l_ui_map` in `src/lupi/api_ui.cpp`) iterates
  `map_data.tilesets` and draws every tileset sub-table present on the
  layer, not just one.
- **Flip flags are baked into the numeric tile id**: `local_id = tile_id &
  0x3FF`, `flip_x = (tile_id & 1024) != 0`, `flip_y = (tile_id & 2048) != 0`
  — this is `lupi-codec`'s own re-encoding of Tiled's GID flip bits
  (`0x80000000`/`0x40000000`), not a newbase invention.
- `cam_x`/`cam_y` passed to `ui.map()` are an **additive** world-placement
  offset for this map/chunk (a real cart draws a chunk at
  `local_tile_pos + mdx/mdy`); the scrolling camera is handled separately
  via the global `ui.camera()`, which every primitive — including
  `ui.map`'s tile blits — still applies on top of this.

## `0b` binary literals

Real Lupi carts use `0b1010...`-style binary integer literals (e.g. for
`ui.fillp` bit patterns) that standard Lua, 5.5 included, doesn't support
(only decimal and `0x` hex). Rather than patch the vendored Lua lexer, the
cart loader (`rloader_lupi_cart` in `src/lupi/loaders.cpp`) rewrites them to
plain decimal literals before compiling, skipping over string/long-string/
comment spans (`lupi_preprocess_binary_literals`) — applied to every sibling
`.lua` file loaded through `require()`, not just `game.lua` itself.

## Deferred work

- **`sfx.*`**: stubs in `src/lupi/api_stubs.cpp`, tagged `// TODO(lupi-audio)`.
  Confirmed via `lupinho` (see "Reference sources") that there's nothing to
  catch up to yet: it registers no `sfx` global at all (no audio feature in
  its README, nothing "sound"/"audio" anywhere in its source) — a cart
  calling `sfx.fx(...)` (e.g. "Balão Gatinho" does) would hit a nil-index
  error in the *official* simulator, so our silent no-op stub is already
  more forgiving than the reference implementation, not behind it. Whenever
  real audio support does land in the Lupi ecosystem, this should wire to
  the `audio` system via `entt::locator`, the same way
  `renderer_service`/`ui_manager` are located in `lupi::init()`.
- **`ui.layout()` / Clay UI**: stubs return empty tables. Same situation as
  `sfx.*` above — confirmed via `lupinho` that there's nothing to catch up
  to yet: no `layout` field on its `ui` table, no `Box`/`Text`/`Image`/
  `Custom`/`Clay` anywhere in its source at all, and neither "Balão
  Gatinho" nor "Caio Pernocas" calls any of these. A separate,
  general-purpose `clay` UI system (vendoring `vendored/clay`) is planned
  independently of `lupi`; once it exists (and/or once a real cart is seen
  using this), these stubs become a thin Lua adapter translating the
  `Box()`/`Text()`/`Image()`/`Custom()` builder tree into that system's
  bindings, reading back render commands into the camera/clip/fillp-aware
  primitives already in `src/lupi/draw.cpp`.

## Debugging

The F10 debug action toggles the "lupi framebuffer" ImGui tool window, showing
the live 480x270 output regardless of whether a scene sprite is showing it.
