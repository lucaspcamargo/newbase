#pragma once

#include <cstdint>
#include <newbase/render/lights.hpp>
#include <newbase/render/pass.hpp>
#include <newbase/render/pipeline.hpp>
#include <newbase/render/types.hpp>
#include <newbase/render/vertex.hpp>
#include <newbase/utility/glm.hpp>


namespace nb  // fwd decls
{
    class rtexture;
    class rmaterial;
    class rshader;
}


namespace nb::render
{

    struct material_texture_binding {
        rtexture* texture {nullptr};
        uint32_t slot {0};
        render::sampler_config sampler {};
        bool vertex {false}; // <-- if we have vertex texture access (e.g. for displacement)
        uint8_t _pad[3];

        bool operator == (const material_texture_binding &other) const
        {
            return texture == other.texture
                && slot == other.slot
                && sampler == other.sampler
                && vertex == other.vertex;
        }
    };

    /// Scene-provided environment resources available to a material draw.
    struct mat_env_data
    {
        glm::vec3 ambient {0.0f};
        bool ibl_enabled {false};
        rtexture *environment_cubemap {nullptr};
        rtexture *irradiance_cubemap {nullptr};
        rtexture *prefiltered_cubemap {nullptr};
    };

    struct pipeline_descriptor; //fwd

    /// A material controller is an interface that handles
    /// the logic for a material type. Mostly (?) stateless.
    /// For now, the types are fixed in the engine.
    class material_controller
    {
    public:

        virtual ~material_controller() = default;

        /// prepares an engine pipeline descriptor for the material
        /// it has to setup shader_variant according to shader selection for the material
        /// may take pipeline configuration (blend, depth test/write) from material,
        /// or optionally filter it or replace it entirely
        /// TODO pass also some global scene rendering settings (when they become necessary)
        /// NOTE: DOES NOT touch pass_target_info
        virtual void make_pipeline_desc(const rmaterial& mat, const mat_env_data *env_data,
                                        pass_type pt, vertex_type vt,
                                        pipeline_descriptor &dst_desc) const = 0;

        /// returns the vertex program associated with this pipeline
        virtual std::shared_ptr<rshader> get_vertex_program(const pipeline_descriptor &desc) const = 0;

        /// returns the fragment program associated with this pipeline
        virtual std::shared_ptr<rshader> get_fragment_program(const pipeline_descriptor &desc) const = 0;

        /// Returns the raw structure size for the vertex shader UBO.
        virtual size_t get_vertex_ubo_size(const pipeline_descriptor &desc,
                                           const mat_env_data *env_data) const = 0;

        /// Returns the raw structure size for the fragment shader UBO.
        virtual size_t get_fragment_ubo_size(const pipeline_descriptor &desc,
                                             const mat_env_data *env_data) const = 0;

        /// generates the fragment uniform buffer for this material
        virtual void pack_vertex_uniforms( const rmaterial& mat, const pipeline_descriptor &desc,
                                           const mat_env_data *env_data,
                                           uint8_t* out_buffer, size_t out_len) const = 0;

        /// generates the fragment uniform buffer for this material
        virtual void pack_fragment_uniforms(const rmaterial& mat, const pipeline_descriptor &desc,
                                            const mat_env_data *env_data,
                                            uint8_t* out_buffer, size_t out_len) const = 0;


        /// returns the number of textures bindings for the shader
        virtual size_t get_num_texture_bindings(const rmaterial& mat, const pipeline_descriptor &dest,
                                                const mat_env_data *env_data) const = 0;

        /// writes all the texture binds required for the shader into the given buffer
        /// the amount to be witten is given by get_num_texture_bindings()
        virtual void collect_texture_bindings( const rmaterial& mat, const pipeline_descriptor &desc,
                                               const mat_env_data *env_data,
                                               material_texture_binding* out_bindings_buf) const = 0;

        /// material controller accessor
        /// controllers are global and static
        static material_controller *get_controller(material_type mt);

        /// frees all material controllers and reference data from the internal
        /// map, releasing textures and other internal resources
        static void release_all_controllers();
    };


    /// standard scene/pass-wide uniforms used by our standard material shaders
    struct alignas(16) scene_uniform_std
    {
        glm::mat4   view;
        glm::mat4   proj;
        glm::mat4   view_proj;
        glm::mat4   inv_view;
        glm::vec3   camera_pos;
        float    _pad0;

        light_uniform_block light_data;
    };

    struct packed_normal_matrix
    {
        packed_normal_matrix(const glm::mat3x3 &src) :
            packed{glm::vec4{src[0][0], src[1][0], src[2][0], 0.0f},
                   glm::vec4{src[0][1], src[1][1], src[2][1], 0.0f},
                   glm::vec4{src[0][2], src[1][2], src[2][2], 0.0f},} {}
        std::array<glm::vec4, 3> packed;   // x, y = col 1, rows 1-2; z, w = col 2, rows 0-1
    };

    struct alignas(16) model_uniforms_std
    {
        glm::mat4 model;
        packed_normal_matrix normal_matrix;  // Transpose(Inverse(Model)) to scale normals correctly
    };

    struct alignas(16) model_uniforms_lit_std
    {
        glm::mat4 model;
        packed_normal_matrix normal_matrix;  // Transpose(Inverse(Model)) to scale normals correctly

        light_model_uniform_block light_indices;
    };
}
