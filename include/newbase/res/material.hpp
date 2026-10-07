#pragma once

#include "entt/core/fwd.hpp"
#include "newbase/render/types.hpp"
#include <newbase/res/resource.hpp>
#include <newbase/render/material.hpp>

// fwd
namespace nb { struct rtexture; }
struct tg3_model;
struct tg3_material;
struct tg3_texture_info;

namespace nb
{

class rmaterial : public resource
{
public:
    explicit rmaterial(entt::id_type id = 0)
    : resource(id, entt::hashed_string{"rmaterial"}.value()) {}


    // the id of the material type of this material
    // defines the set of parameters contained within
    render::material_type mat_type;

    // the material parameter block
    // must hold a single struct of a type known to the rtti system
    // this shall make our material editor as simple as possible
    // for now we shove shared_ptr<rtexture> in the struct too...
    entt::meta_any mat_params;

    // the parameters of the fixed-function part of the rendering
    // pipeline, which are common to every material
    render::pipeline_params pipeline_params;

    // uniform buffer to be bound for fragment shader usage
    std::vector<uint8_t> vert_uniforms;

    // uniform buffer to be bound for fragment shader usage
    std::vector<uint8_t> frag_uniforms;

    // whether the material need to be regenerated from changed parameters
    bool dirty {true};

    bool materialize();

    render::material_controller* controller() const { return m_controller; }

    // rtti
    static void _init_rtti();

protected:
    bool do_load() override;

private:
    bool _load_gltf_model_material(const tg3_model *model, const tg3_material *mat, const std::string &parent_path);
    std::shared_ptr<rtexture> _resolve_texture(const tg3_model *model, int32_t index, const std::string &parent_path);
    void _setup_sampler(const tg3_model *model, const tg3_texture_info &tex_info, render::sampler_config &sc);

    render::material_controller *m_controller {nullptr};
};

}
