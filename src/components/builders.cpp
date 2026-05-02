#include <newbase/components/builders.hpp>
#include <newbase/components/spatial.hpp>
#include <newbase/components/sprite.hpp>
#include <newbase/components/script.hpp>
#include <newbase/components/body2d.hpp>
#include <newbase/components/particle_emitter.hpp>
#include <newbase/components/textext.hpp>
#include <newbase/components/tilemap.hpp>
#include <newbase/components/character2d.hpp>
#include <newbase/res/particle_emitter.hpp>
#include <newbase/res/texfont.hpp>
#include <newbase/res/tilemap.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/yaml/glm.hpp>
#include <newbase/log.hpp>

#include <entt/entt.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <ryml.hpp>
#include <ryml_std.hpp>


using namespace nb;

bool ::nb::build_spatial(ryml::ConstNodeRef def, cspatial &dst)
{
    if(def.has_child("pos"))
        load_vec3(def["pos"], dst.pos);
    if(def.has_child("rot"))
        load_vec3(def["rot"], dst.rot);
    if(def.has_child("scale"))
        load_vec3(def["scale"], dst.scale);
    
    dst.apply(); // TODO this has to be handled in spatial subsystem
    return true;
}

bool ::nb::build_sprite(ryml::ConstNodeRef def, csprite &dst)
{
    // TODO should components have resource handles or just ids?
    std::string respath;
    c4::from_chars(def["res"].val(), &respath);
    auto hash = entt::hashed_string(respath.c_str());
    dst.spr = rman().get<rsprite>(hash.value());
    if(def.has_child("color"))
        load_vec4(def["color"], dst.color);
    if(def.has_child("visible"))
        def["visible"] >> dst.visible;
    if(def.has_child("pixel_snap"))
        def["pixel_snap"] >> dst.pixel_snap;
    if(def.has_child("animating"))
        def["animating"] >> dst.animating;
    if(def.has_child("sequence"))
    {
        std::string seq;
        c4::from_chars(def["sequence"].val(), &seq);
        dst.sequence = seq;
    }
    return true;
}

bool ::nb::build_script(ryml::ConstNodeRef def, cscript &dst)
{
    // TODO should components have resource handles or just ids?
    std::string respath;
    c4::from_chars(def["lua"].val(), &respath);
    auto hash = entt::hashed_string(respath.c_str());
    dst.script = rman().get<rscript>(hash.value());
    return true;
}


bool nb::build_body2d(ryml::ConstNodeRef def, cbody2d &dst)
{
    if(def.invalid())
        return false;
    
    if(def.has_child("type"))
    {
        const auto type_str = def["type"].val();
        if(type_str == "STATIC")
            dst.type = body2d_type::STATIC;
        else if(type_str == "KINEMATIC")
            dst.type = body2d_type::KINEMATIC;
        else if(type_str == "DYNAMIC")
            dst.type = body2d_type::DYNAMIC;
        else
            log::warn("[build_body2d] unknown body type!");
    }

    if(def.has_child("gravity_scale"))
        def["gravity_scale"] >> dst.gravity_scale;
    if(def.has_child("linear_damping"))
        def["linear_damping"] >> dst.linear_damping;
    if(def.has_child("angular_damping"))
        def["angular_damping"] >> dst.angular_damping;

    if(def.has_child("shapes") && def["shapes"].is_seq())
    {
        for(ryml::ConstNodeRef sdef: def["shapes"])
        {
            shape2d shape;
            
            const auto stype = sdef["type"].val();
            if(stype == "BOX")
                shape.shape_type = shape2d_type::BOX;
            else if(stype == "CIRCLE")
                shape.shape_type = shape2d_type::CIRCLE;
            else if(stype == "POLY")
                shape.shape_type = shape2d_type::POLY;

            sdef["data"] >> shape.shape_data;

            if(sdef.has_child("sensor"))
                sdef["sensor"] >> shape.sensor;
            if(sdef.has_child("sensor_events"))
                sdef["sensor_events"] >> shape.sensor_events;
            if(sdef.has_child("contact_events"))
                sdef["contact_events"] >> shape.contact_events;
            if(sdef.has_child("category_bits"))
                sdef["category_bits"] >> shape.category_bits;
            if(sdef.has_child("mask_bits"))
                sdef["mask_bits"] >> shape.mask_bits;

            dst.shapes.push_back(shape);
        }
    }

    return true;
}

bool nb::build_particle_emitter(ryml::ConstNodeRef def, cparticle_emitter &dst)
{
    if (def.has_child("res"))
    {
        std::string respath;
        c4::from_chars(def["res"].val(), &respath);
        auto hash = entt::hashed_string(respath.c_str());
        dst.res = rman().get<rparticle_emitter>(hash.value());
    }
    if (def.has_child("emitting"))
        def["emitting"] >> dst.emitting;
    return true;
}

bool nb::build_tilemap(ryml::ConstNodeRef def, ctilemap &dst)
{
    if (def.has_child("res"))
    {
        std::string respath;
        c4::from_chars(def["res"].val(), &respath);
        dst.map = rman().get<rtilemap>(entt::hashed_string{respath.c_str()}.value());
    }
    if (def.has_child("render_layer"))
    {
        std::string s;
        def["render_layer"] >> s;
        dst.render_layer = std::move(s);
    }
    if (def.has_child("collision_layer"))
    {
        std::string s;
        def["collision_layer"] >> s;
        dst.collision_layer = std::move(s);
    }
    if (def.has_child("visible"))
        def["visible"] >> dst.visible;
    return true;
}

bool nb::build_character2d(ryml::ConstNodeRef def, ccharacter2d &dst)
{
    if (def.has_child("capsule_radius"))
        def["capsule_radius"] >> dst.capsule_radius;
    if (def.has_child("capsule_half_height"))
        def["capsule_half_height"] >> dst.capsule_half_height;
    if (def.has_child("gravity_scale"))
        def["gravity_scale"] >> dst.gravity_scale;
    if (def.has_child("category_bits"))
        def["category_bits"] >> dst.category_bits;
    if (def.has_child("mask_bits"))
        def["mask_bits"] >> dst.mask_bits;
    if (def.has_child("push_force"))
        def["push_force"] >> dst.push_force;
    return true;
}

bool nb::build_textext(ryml::ConstNodeRef def, ctextext &dst)
{
    if (def.has_child("font"))
    {
        std::string respath;
        c4::from_chars(def["font"].val(), &respath);
        dst.font = rman().get<rtexfont>(entt::hashed_string{respath.c_str()}.value());
    }
    if (def.has_child("text"))
    {
        std::string t;
        def["text"] >> t;
        dst.text  = std::move(t);
        dst.dirty = true;
    }
    if (def.has_child("color"))
        load_vec4(def["color"], dst.color);
    return true;
}
