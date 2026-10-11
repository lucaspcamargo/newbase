#pragma once

#include <newbase/render/material.hpp>
#include <newbase/render/batcher2d.hpp>

namespace nb::render
{
    // The standard 2d rendering material.
    // Not used by render_2d, this is for renderers with a full 3D pipeline.
    // render_2d uses SDL_Renderer directly.
    // The associated material instance is only used for forwarding blend mode
    // When setting up pipelines, use the provided rmaterial instances
    // NOTE 2D still needs special handling, but conceivably, we could also
    //      plug batcher_2d into collector_3d in the future by using this,
    //      if we start handling texture bindings. It would require a special
    //      streaming mesh instance.

    class mat_std_2d : public material_controller
    {
    public:
        mat_std_2d();
        ~mat_std_2d() override;

        /// Provides the material instance to use for setting up pipelines for a
        /// drawing command
        static rmaterial * get_token_material(const command2d &cmd);

        std::vector<rshader*> get_all_shaders()
        {
            return {m_vert.get(), m_frag_notex.get(), m_frag_tex.get()};
        }

        // controller impl

        void make_pipeline_desc(const rmaterial& mat, pass_type pt, vertex_type vt,
                                pipeline_descriptor &dst_desc) const override;

        std::shared_ptr<rshader> get_vertex_program(const pipeline_descriptor& pd) const override;

        std::shared_ptr<rshader> get_fragment_program(const pipeline_descriptor& pd) const override;

        size_t get_vertex_ubo_size(const pipeline_descriptor& pd) const override { return 0; }

        size_t get_fragment_ubo_size(const pipeline_descriptor& pd) const override { return 0; }

        void pack_vertex_uniforms( const rmaterial&, const pipeline_descriptor&, uint8_t*,
                                    size_t) const override {} // do nothing

        void pack_fragment_uniforms(const rmaterial&, const pipeline_descriptor&, uint8_t*,
                                    size_t) const override {} // do nothing

        // NOTE we don't handle texture bindings right now, The material is only
        //      used for storing blend mode. The renderer must bind textures itself
        //      from batcher2d drawing commands.
        size_t get_num_texture_bindings(const rmaterial& mat, const pipeline_descriptor &dest) const override { return 0; }
        void collect_texture_bindings( const rmaterial& mat, const pipeline_descriptor& pd, material_texture_binding *out) const override {}

    private:
        std::shared_ptr<rshader>  m_vert;
        std::shared_ptr<rshader>  m_frag_notex;
        std::shared_ptr<rshader>  m_frag_tex;
    };
}
