#pragma once

#include <newbase/render/types.hpp>
#include <newbase/utility/glm.hpp>
#include <vector>
#include <memory>


namespace nb  // fwd decls
{
    class rtexture;
    class rmaterial;
    class rshader;
}


namespace nb::render
{
    /// These are the parameters that are part of a material,
    /// but are intrinsic the the fixed function pipeline,
    /// so they are present for every material type.
    /// Mostly rasterization control parameters.
    struct pipeline_params
    {
        blendmode blend {blendmode::NONE};
        bool cull_back {true};
        bool depth_test {true};
        bool depth_write {true};
    };

    struct material_texture_binding {
        uint32_t slot {0};
        std::shared_ptr<rtexture> texture {nullptr};
        render::sampler_config sampler {};
        bool vertex {false}; // <-- if we have vertex texture access (e.g. for displacement)
        // TODO? optional per-slot sampler override parameters?
        //       do that when we use a sampler cache, like for pipelines
    };

    enum class material_type : uint8_t
    {
        INVALID,
        STANDARD_PBR,
        DYNAMIC  // TODO for custom/scriptable material types in the future
    };

    /// A material controller is an interface that handles
    /// the logic for a material type. Mostly (?) stateless.
    /// For now, the types are fixed in the engine.
    class material_controller
    {
    public:
        using tex_bind_vec_t = std::vector<material_texture_binding>;

        virtual ~material_controller() = default;

        /// returns the vertex program associated with this material type
        virtual std::shared_ptr<rshader> get_vertex_program(const rmaterial& mat) const = 0;

        /// returns the fragment program associated with this material type
        virtual std::shared_ptr<rshader> get_fragment_program(const rmaterial& mat) const = 0;

        /// returns the raw structure size for the vertex shader ubo
        virtual size_t get_vertex_ubo_size(const rmaterial& mat) const = 0;

        /// returns the raw structure size for the fragment shader ubo
        virtual size_t get_fragment_ubo_size(const rmaterial& mat) const = 0;

        /// generates the fragment uniform buffer for this material
        virtual void pack_vertex_uniforms( const rmaterial& mat,
                                           uint8_t* out_buffer, size_t out_len) const = 0;

        /// generates the fragment uniform buffer for this material
        virtual void pack_fragment_uniforms(const rmaterial& mat,
                                            uint8_t* out_buffer, size_t out_len) const = 0;

        /// returns all the textures required for the shader
        virtual void collect_texture_bindings( const rmaterial& mat,
                                               tex_bind_vec_t& out_bindings) const = 0;

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
        glm::vec3   light_dir;      // Normalized direction towards light source
        float    _pad1;
        glm::vec3   light_color;    // Light radiance (color * intensity)
        float    _pad2;
    };

    struct alignas(16) model_uniforms_std
    {
        glm::mat4 model;
        glm::mat4 normal_matrix;  // Transpose(Inverse(Model)) to scale normals correctly
    };

}
