#pragma once

#include "newbase/render/pass.hpp"
#include <newbase/render/pipeline.hpp>
#include <newbase/render/material.hpp>
#include <newbase/layer.hpp>
#include <cstdint>
#include <memory>
#include <vector>

// our 3d render command collector and batcher

// since we don't build our geometry buffer on the CPU like for 2D,
// we don't use a separate geometry batcher, and command merging is off the table
// We provide the ordering, and binding optimizations are done opportunistically
// by the renderer after command sorting.

// commands are split into higher-level layer sync points (like in batcher2d),
// then in groups (opaque front-to-back vs transparent back-to-front), then
// ordered depending on the bits of the specific group format.

// See implementation TU for command sort_key specifics.

namespace nb
{
    class rmesh;
    class rmaterial;
    class rshader;
}
struct SDL_GPUGraphicsPipeline;

namespace nb::render
{
    static constexpr size_t COLLECTOR_3D_DEFAULT_CMD_ALLOC_COUNT = 1024;
    static constexpr size_t COLLECTOR_3D_DEFAULT_ARENA_ALLOC_BYTES = 2048*1024; // 2 MiB
    static constexpr size_t COLLECTOR_3D_MAX_PIPELINES = 8192;
    static constexpr size_t COLLECTOR_3D_MAX_TEXTURES = 8192;
    static constexpr size_t COLLECTOR_3D_MAX_SHADERS = 2048;
    static constexpr size_t COLLECTOR_3D_MAX_MESHES = 2048;

    static constexpr int CMD_3D_MAX_TEXTURES = 8;


    // this is a draw command
    // uniform data generated during collection (except global scene uniforms)
    // lives in the collector arena and is stored as offsets (+ size/count when it varies)
    // a zero size or UINT32_MAX start offset means a binding does not exist
    struct command3d
    {
        uint64_t sort_key;

        rmaterial *material {nullptr}; // TODO perhaps also add to staging vectors?
        uint32_t mat_vert_uniforms_offset {UINT32_MAX};
        uint32_t mat_vert_uniforms_len {0};
        uint32_t mat_frag_uniforms_offset {UINT32_MAX};
        uint32_t mat_frag_uniforms_len {0};

        uint32_t model_uniforms_offset {UINT32_MAX}; // model_uniforms_std in arena

        uint16_t pipeline_index {0xffff}; // in collector's pipeline data arrays
        uint16_t mesh_index {0xffff}; // in collector's mesh registry
        uint16_t submesh {0xffff};

        uint8_t  _pad0 {0};
        uint8_t  tex_bind_count {0};
        uint32_t tex_binds_offset {UINT32_MAX}; // array of render::material_texture_binding, in arena
    };

    /// memory arena for the collector3d
    /// a reusable buffer, not reallocated every frame
    class coll3d_frame_arena
    {
    private:
        std::vector<uint8_t> m_buffer;
        size_t m_offset{ 0 };

    public:
        explicit coll3d_frame_arena(size_t capacity_bytes) {
            m_buffer.resize(capacity_bytes);
        }

        void reset() {
            m_offset = 0; // instant clear
        }

        template <typename T, typename... Args>
        T* alloc(Args&&... args) {
            size_t size = sizeof(T);
            // align to 16 butes for GPU API safety
            size_t aligned_offset = (m_offset + 15) & ~15;

            if (aligned_offset + size > m_buffer.size()) {
                m_buffer.resize(m_buffer.size() * 2); // grow
            }

            T* ptr = reinterpret_cast<T*>(&m_buffer[aligned_offset]);
            new (ptr) T(std::forward<Args>(args)...); // construct in-place

            m_offset = aligned_offset + size;
            return ptr;
        }

        // if data == nullptr, will just return allocate block address without
        // initializaion
        void* alloc_raw(const void* data, size_t size) {
            size_t aligned_offset = (m_offset + 15) & ~15;
            if (aligned_offset + size > m_buffer.size()) {
                m_buffer.resize(m_buffer.size() * 2);
            }

            void* dest = &m_buffer[aligned_offset];
            if(data)
                std::memcpy(dest, data, size);

            m_offset = aligned_offset + size;
            return dest;
        }

        uint32_t byte_offset(const void* in_arena) const noexcept
        {
            if (in_arena == nullptr) return UINT32_MAX;
            const uint8_t* ptr = static_cast<const uint8_t*>(in_arena);
            assert(ptr >= m_buffer.data() && ptr <= (m_buffer.data() + m_buffer.capacity()) &&
            "Pointer does not belong to this arena!");
            return static_cast<uint32_t>(ptr - m_buffer.data());
        }

        const uint8_t *raw_data() const { return m_buffer.data(); }
    };


    /// Our generalized 3D rendering cmmand collector
    /// Should be used by every renderer supporting 3D
    /// Note that we hold raw pointers to resources, and indices into internal
    /// buffers. This data should stay valid only during the current rendering
    /// steps.
    class collector3d final : public pass_build_context
    {
    public:
        /// a layer_markes is basically the same as a batcher2d sync_point
        /// it groups drawing commands for a render layer together
        /// so we can have one collector for all layers
        struct layer_marker
        {
            uint32_t pass_start {0};
            uint32_t pass_count {0};
        };

        /// Collector-owned command range and scene data associated with a pass.
        struct collected_pass
        {
            uint32_t desc_offset {UINT32_MAX};
            uint32_t cmd_start {0};
            uint32_t cmd_count {0};
            uint32_t scene_uniforms_offset {UINT32_MAX};
            uint32_t scene_uniforms_size {0};
        };

        /// clears all internal data lists and resets arena
        /// makes collector ready for a new frame
        void clear();

        /// Collect the passes required for one render layer. Passes are selected
        /// by the collector from layer and scene data.
        void collect(const render_layer &l, const pass_target_info &layer_targets);

        bool append_draw_pass(const render_pass &pass,
                              std::shared_ptr<rmaterial> material,
                              std::shared_ptr<rmesh> mesh,
                              const void *scene_uniforms = nullptr,
                              size_t scene_uniforms_size = 0) override;

        // inline getters

        inline const std::vector<command3d>& commands() const { return m_cmds; }
        inline const std::vector<layer_marker>& markers() const { return m_layer_markers; }
        inline const std::vector<collected_pass>& passes() const { return m_passes; }
        inline const auto& pipeline_hashes() const { return m_pipeline_hashes; }
        inline const auto& pipeline_descriptors() const { return m_pipeline_descs; }
        inline uint16_t pipeline_count() const { return m_pipeline_count; }

        inline const auto& staging_textures() const { return m_staging_textures; }
        inline uint16_t staging_texture_count() const { return m_staging_texture_count; }
        inline const auto& staging_shaders() const { return m_staging_shaders; }
        inline uint16_t staging_shader_count() const { return m_staging_shader_count; }
        inline const auto& staging_meshes() const { return m_staging_meshes; }
        inline uint16_t staging_mesh_count() const { return m_staging_mesh_count; }
        inline const void * arena_ptr(uint32_t byte_offset) const { return m_arena.raw_data() + byte_offset; }
        inline const render_pass& pass_desc(const collected_pass &pass) const
        {
            return *static_cast<const render_pass*>(arena_ptr(pass.desc_offset));
        }

    private:
        void _sort(const collected_pass &pass);
        void _collect_pass(const render_layer &layer, const render_pass &pass,
                           const mat_env_data *env_data);
        uint16_t _get_pipeline_index(const pipeline_descriptor & desc);
        uint16_t _add_staging_texture(rtexture* tex);
        uint16_t _add_staging_shader(rshader* shd);
        uint16_t _add_staging_mesh(rmesh* shd);

        std::vector<layer_marker> m_layer_markers {};
        std::vector<collected_pass> m_passes {};
        std::vector<command3d> m_cmds {}; // reserved at construction time
        std::array<pipeline_descriptor::hash_t, COLLECTOR_3D_MAX_PIPELINES> m_pipeline_hashes;
        std::array<pipeline_descriptor, COLLECTOR_3D_MAX_PIPELINES> m_pipeline_descs;
        uint16_t m_pipeline_count {0};
        std::array<rtexture*, COLLECTOR_3D_MAX_TEXTURES> m_staging_textures;
        uint16_t m_staging_texture_count {0};
        std::array<rshader*, COLLECTOR_3D_MAX_SHADERS> m_staging_shaders;
        uint16_t m_staging_shader_count {0};
        std::array<rmesh*, COLLECTOR_3D_MAX_MESHES> m_staging_meshes;
        uint16_t m_staging_mesh_count {0};
        std::vector<std::shared_ptr<rmaterial>> m_internal_materials;
        coll3d_frame_arena m_arena {COLLECTOR_3D_DEFAULT_ARENA_ALLOC_BYTES};
    };
}
