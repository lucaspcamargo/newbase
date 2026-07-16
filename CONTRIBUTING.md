# Contributing to newbase

Thanks for taking a look! `newbase` is a young, mostly solo-maintained engine, so this
document stays short on purpose. If something below is unclear, just ask before
sinking time into a large change.

## Before you start

For anything beyond a small fix (a new system, a change to an existing system's public
API, a new supported platform), please open an issue first to talk it through. It's a
lot cheaper to align on approach before code exists than to rework a finished PR.

## Building

See the [README](README.md) for platform build instructions and dependencies. In short:
CMake + Python 3.10+ (for the resource indexer and other build-time scripts) on all
platforms, plus whatever platform SDK is relevant (Emscripten SDK, Android NDK, MXE for
Windows cross-builds, etc).

There is currently no automated test suite. Verification means: build cleanly, then
actually run the affected demo and exercise the feature (see `demo/`, launched via
`newbase_demo`). If your change touches a system with a debug tool window (e.g.
`physics2d`'s, opened with **F8**), use it to sanity-check the change visually.

## Architecture orientation

Skim these before writing a new system or touching an existing one — they cover
patterns used consistently across the codebase:

- **Systems** derive from `nb::system` and implement `step(step_phase)`. Phases run in
  order: `PREPARE` → `PHYSICS_UPDATE` → `GENERAL_UPDATE` → `PRE_RENDER`. Pick the phase
  that matches what your system depends on / produces.
- **Components** are plain structs (no logic) with a `static void _ensure_rtti()`,
  registered via `entt::meta_factory` in `src/components/*_rtti.cpp`.
- **RTTI registration** is how everything gets discovered — by the editor, by
  serialization, and by Lua. A type needs `entt::meta_factory<T>{}.type(...)
  .custom<rtti::type_info>(...)` with the right `type_class` (`TYPE_CLASS_SYSTEM`,
  `TYPE_CLASS_COMPONENT`, `TYPE_CLASS_SERVICE`, ...), plus `.func<>().custom<rtti::func_info>()`
  for each method you want exposed.
- **Getting something into Lua is, in practice, "register it correctly on the RTTI
  subsystem"** — there's no separate manual binding step. Once a system function has
  `rtti::func_info`, it's automatically exposed as `{system_identifier}_{func_name}(...)`;
  service getters become `svc_{identifier}()`; component getters become
  `get_{identifier}(eid)` globally and `c_{identifier}()` inside a script's own entity
  environment. See `src/script_lua/script_lua.cpp` (`bind_systems`, `bind_services`,
  `bind_component_getters`) if you need the mechanics, but you usually won't — just
  register the RTTI correctly and the Lua binding follows for free.

## Adding a new system

1. `src/<name>/` with a `CMakeLists.txt` calling `newbase_add_system(NAME <name>
   SOURCES ...)` then `newbase_commit_systems()`.
2. Header in `include/newbase/<name>/<name>.hpp`, deriving from `nb::system`.
3. `extern "C" void _rtti_init_<name>()` in the `.cpp`, registering the type and its
   exposed functions as above.
4. If it needs config, read it from the `ryml::ConstNodeRef cfg` passed to `init()`.

Look at `src/physics2d` or `src/input` for a complete, moderately-sized example.

## Code style

- Match the surrounding code's style rather than introducing a new one in the same
  file. No strong house style is enforced beyond that yet.
- Prefer small, focused commits over large mixed ones — makes bisecting and review
  easier.
- Commit messages follow `[area] short imperative summary`, e.g. `[physics2d] add
  point_query for script-driven picking`. Multiple areas can be joined with `;`, e.g.
  `[demo;input] ...`. Append `[no ci]` for commits that don't need CI to run (docs-only,
  comment fixes).

## License

By submitting a contribution, you agree it's provided under the project's
[BSD-3-Clause license](LICENSE).
