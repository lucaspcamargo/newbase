# render_gpu — Implementation Overview

## 1. Resources created at init

Everything permanent is created once in `init()`:

| Resource | What it is |
|---|---|
| `_win` | SDL3 window |
| `_device` | SDL_GPU device (Vulkan/Metal/D3D12 via SDL_shadercross) |
| `_pipeline_sprite` | Graphics pipeline: sprite.vert + sprite.frag, non-indexed, with alpha blend |
| `_pipeline_mesh2d` | Graphics pipeline: mesh2d.vert + mesh2d.frag, indexed, with alpha blend |
| `_default_sampler` | Nearest-filter sampler used by all textured draws |
| `_vbuf_sprite` | GPU vertex buffer — holds all sprite quads for the frame |
| `_vbuf_mesh` | GPU vertex buffer — holds all mesh vertices for the frame |
| `_ibuf_mesh` | GPU index buffer — holds all mesh indices for the frame |
| `_tbuf_sprite`, `_tbuf_mesh`, `_titbuf_mesh` | CPU-visible **transfer buffers** — staging area for CPU→GPU upload |

Both pipelines share the same 32-byte vertex layout: `float x, y, u, v, r, g, b, a` (pos2, uv2, color4), matching the C++ `sprite_vertex`/`mesh_vertex` structs exactly.

Both vertex shaders declare a single UBO at `layout(set=1, binding=0)` containing a `mat4 viewproj`, pushed per draw call via `SDL_PushGPUVertexUniformData`.

---

## 2. The per-frame render pipeline (RENDER phase)

The `step(RENDER)` function runs everything in this order:

### A. Geometry collection (CPU)

Two camera paths both feed into the same `_draw_scene()` function:

- **No layers (fallback):** Uses `_fallback_spatial.pos` (set by `cam_2d_setup`) and `_fallback_camera.zoom` to build `vp_mat`.
- **Layer path:** For each `render_layer`, reads `cspatial.pos` + `ccamera.zoom` from the layer's camera *entity*.

The viewproj matrix:
```
vp_mat = scale(2·zoom/vp.w,  -2·zoom/vp.h,  1)
       × translate(-cam_x, -cam_y, 0)
```
The Y scale is **negative** because SDL_GPU NDC is y-up (lower-left = (−1,−1)) while the game world is y-down. This maps world y=0 (top) → NDC y=+1 (top), world y=H (bottom) → NDC y=−1 (bottom).

`_draw_scene()` iterates every entity with `cspatial`, filtered by `clayers.mask`. For each:

- **`csprite`:** Looks up the texture in `_tex_cache`. Skips if not ready. Reads `current_source_rect` to compute `u0,v0,u1,v1`. Computes the quad corners in **world space** (applying `spatial.world`). Emits 6 vertices (2 triangles) into `sprite_verts`. Merges the draw call with the previous one if same texture + same `viewproj`.
- **`cmesh2d`:** Copies all geometry vertices (world-transformed) into `mesh_verts` and indices into `mesh_indices`. If `cmesh2d.tex` is set and ready, emits a `MESH_TEX` draw call (sprite pipeline + indexed draw); otherwise a `MESH` draw call (mesh pipeline + indexed draw).

The result: two flat vertex arrays (`sprite_verts`, `mesh_verts`), one index array (`mesh_indices`), and a list of `draw_call` structs describing slices of those arrays.

### B. CPU→GPU upload (copy pass)

The transfer buffers are mapped, vertices/indices are `memcpy`'d in, then unmapped. A **copy pass** is then opened to `SDL_UploadToGPUBuffer` the data to the permanent GPU-resident `_vbuf_*` / `_ibuf_mesh`.

### C. Texture upload (copy pass, lazy)

Any `rtexture` in `_tex_cache` that has a pending `SDL_Surface* surf` (just loaded from disk) gets uploaded here via `_upload_scene_tex`, which:
1. Creates a GPU texture
2. Creates a temporary transfer buffer, copies pixel data row-by-row (handling non-packed pitch), uploads, then releases the transfer buffer
3. Sets `entry.ready = true`, frees the CPU surface

### D. Render pass

A single render pass targets the swapchain texture. Clear color comes from the first layer's `clear_*` values.

The draw loop iterates `draw_calls` in order:
1. `SDL_PushGPUVertexUniformData(cmd, 0, &dc.viewproj, 64)` — pushes the 4×4 matrix for this draw's camera/layer
2. Lazy-binds the pipeline (only switches when the draw call kind changes)
3. Lazy-binds the texture sampler (only when the texture changes)
4. Issues `SDL_DrawGPUPrimitives` (sprites) or `SDL_DrawGPUIndexedPrimitives` (meshes)

Finally, ImGui's draw data is rendered via `ImGui_ImplSDLGPU3_RenderDrawData` into the same render pass, then the pass ends and the command buffer is submitted.

---

## 3. Key structural characteristics

**Single-pass, all-layers, one command buffer per frame.** All layers' geometry is batched into a single vertex buffer upload and a single render pass. There is no per-layer clear or per-layer viewport scissoring yet (the `viewport_entry` data exists but `SDL_SetGPUViewport` is never called).

**Vertex storage is world-space.** The CPU only applies `spatial.world` (entity transform); the camera/projection part is the shader's job via the pushed `viewproj` uniform.

**Draw call merging is conservative.** Consecutive sprite draw calls with the same texture *and* same `viewproj` are merged into one. Cross-layer merging never happens because `viewproj` changes between layers.

**Texture cache is eternal.** `_tex_cache` maps `rtexture*` → `scene_tex_entry`; entries are never evicted. The CPU-side `SDL_Surface` is freed after upload; the GPU texture lives until the renderer is destroyed.

**Two separate vertex pools, each capped at 65536 vertices.** Sprites and meshes each have their own buffer set. No overflow handling — exceeding the cap silently produces incorrect draws.
