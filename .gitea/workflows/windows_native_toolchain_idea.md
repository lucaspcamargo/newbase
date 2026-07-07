# Idea: native Windows CI runner (replacing/augmenting MXE cross-compile)

Not a workflow — a design note. `windows_build.yaml` currently cross-compiles
for Windows from an Ubuntu container using MXE (MinGW-w64 under the hood).
This note tracks why that setup hit a wall and what to evaluate once a
Windows 11 VM is available as a native Gitea Actions runner host.

## Motivation

`SDL_shadercross` vendors `DirectXShaderCompiler` (DXC) to compile HLSL to
DXIL/SPIR-V (`SDL_ShaderCross_CompileSPIRVFromHLSL`, `SDL_shadercross.c:629`).
DXC only builds if `SDLSHADERCROSS_DXC` is on (`NEWBASE_ENABLE_DXC` in the
top-level `CMakeLists.txt`, defaulted from `${WIN32}`).

Enabling it under the current MXE/MinGW-w64 cross-compile toolchain fails:
DXC's `dxcapi.h` takes its native-Windows branch whenever `_WIN32` is
defined (true for any Windows target, not just MSVC) and skips
`WinAdapter.h`, the portability shim that defines `BOOL`/`HANDLE`/etc. for
non-MSVC toolchains (`external/DirectXShaderCompiler/include/dxc/dxcapi.h:16-38`).
That branch also uses `__declspec(uuid(...))`, an MSVC-only extension GCC/
MinGW doesn't implement. DXC's Windows-target code path effectively only
supports MSVC or `clang-cl` — there's no MinGW-GCC adapter the way there is
for Linux/macOS.

Current mitigation: `NEWBASE_ENABLE_DXC` stays off for this toolchain, so
Windows builds lose D3D12/DXIL shader compilation (D3D11/DXBC via
`d3dcompiler_47.dll` at runtime, and Vulkan/Metal, are unaffected).
HLSL-as-authoring-source input is unavailable everywhere without DXC.

## Options once a native Windows 11 host is available

1. **Native MSVC build on the Windows 11 VM.** Straightforward, DXC builds
   as intended, no cross-compile toolchain quirks. Cost: a second runner
   image/pipeline to maintain, likely slower/more expensive than the
   container-based Linux runners.
2. **`clang-cl` + `lld-link` cross-compile from the existing Ubuntu
   container**, using [`xwin`](https://github.com/Jake-Shadle/xwin) to
   fetch Windows SDK/MSVC CRT headers and import libs without a Windows
   license. CMake already treats clang-cl as `MSVC` (sets the `MSVC`
   variable via the simulated frontend), so existing `if(MSVC)` /
   `if(NOT MSVC)` branches in this repo's CMake would behave correctly
   without changes. DXC should build since clang-cl supports
   `__declspec(uuid(...))` and MS extensions. Keeps everything in the
   existing container-based CI shape — no native Windows runner needed.
   Untested here; worth a spike before committing to it.
3. **Keep MXE, DXC off.** Current state. Simplest, but no D3D12/DXIL and
   no HLSL-source compilation on the Windows leg.

## Open questions to resolve before choosing

- Do we actually need D3D12/DXIL, or is D3D11/DXBC (already working,
  no DXC needed) sufficient for the foreseeable target hardware?
- If HLSL-as-authoring-source becomes compelling (see prior discussion —
  DXC is portable enough to want on Linux too, not just Windows), that
  tips the scale toward getting DXC working *somewhere* reliable, which
  favors option 1 or 2 over 3.
- Build time impact of DXC (the whole reason this got scrutinized in the
  first place) applies to options 1 and 2 alike — cache the DXC build
  output either way once enabled.
