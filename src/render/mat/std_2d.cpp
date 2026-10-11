#include "entt/core/hashed_string.hpp"
#include <newbase/render/mat/std_2d.hpp>
#include <newbase/render/types.hpp>
#include <newbase/render/vertex.hpp>
#include <newbase/res/material.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/res/shader.hpp>

using namespace nb;
using namespace nb::render;
using entt::operator""_hs;

static uint16_t SHADER_VARIANT_TEXTURED_BIT = 1<<15;



// static token materials. one for each blend mode and textured/untextured combo
// they hold no resources and are just released at exit
using mat_map = std::unordered_map<uint16_t, rmaterial*>;

static mat_map& get_mat_map()
{
    static mat_map m = mat_map();
    return m;
}

mat_std_2d::mat_std_2d()
{
    m_vert = rman().load_sync<rshader>("_nb_core/slang/vertex2d.vert.slang"_hs);
    m_frag_notex = rman().load_sync<rshader>("_nb_core/slang/untextured.frag.slang"_hs);
    m_frag_tex = rman().load_sync<rshader>("_nb_core/slang/textured.frag.slang"_hs);
}

mat_std_2d::~mat_std_2d()
{
    // relase shaders
    m_vert.reset();
    m_frag_notex.reset();
    m_frag_tex.reset();

    // delete token materials
    for ( auto [_, mat_ptr] : get_mat_map() )
    {
        delete mat_ptr;
    }
}


rmaterial * mat_std_2d::get_token_material(const command2d &cmd)
{
    mat_map &mats = get_mat_map();
    uint16_t variant = ((uint16_t)blendmode::NONE) | (cmd.texture != -1? SHADER_VARIANT_TEXTURED_BIT : 0);
    auto it = mats.find(variant);
    if(it == mats.end())
    {
        rmaterial * new_mat = new rmaterial();
        new_mat->mat_params = variant;
        new_mat->pipeline_params = pipeline_params
        {
            .blend = cmd.blend,
            .cull = cull_mode::NONE,
            .depth_test = false,
            .depth_write = false
        };
        it = mats.emplace(variant, new_mat).first;
    }
    return it->second;
}


// controller impl

void mat_std_2d::make_pipeline_desc(const rmaterial& mat, pass_type pt, vertex_type vt,
                                    pipeline_descriptor &dst_desc) const
{
    assert(vt == vertex_type::STANDARD_2D);

    const auto variant = mat.mat_params.cast<uint16_t>();
    const bool textured = variant & SHADER_VARIANT_TEXTURED_BIT;

    dst_desc = {};
    dst_desc.vertex_fmt = vertex_type::STANDARD_2D;
    dst_desc.mat_type = material_type::STANDARD_2D;
    dst_desc.params = mat.pipeline_params;
    dst_desc.shader_variant = textured? 1 : 0;
}

std::shared_ptr<rshader> mat_std_2d::get_vertex_program(const pipeline_descriptor& pd) const
{
    return m_vert;
}

std::shared_ptr<rshader> mat_std_2d::get_fragment_program(const pipeline_descriptor& pd) const
{
    if(pd.shader_variant)
        return m_frag_tex;
    else
        return m_frag_notex;
}
