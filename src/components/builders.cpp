#include <newbase/components/builders.hpp>
#include <newbase/components/body2d.hpp>
#include <newbase/components/camera.hpp>
#include <newbase/components/character2d.hpp>
#include <newbase/components/mesh.hpp>
#include <newbase/components/particle_emitter.hpp>
#include <newbase/components/script.hpp>
#include <newbase/components/spatial.hpp>
#include <newbase/components/sprite.hpp>
#include <newbase/components/textext.hpp>
#include <newbase/components/tilemap.hpp>
#include <newbase/components/layers.hpp>
#include <newbase/sys/textext/rtexfont.hpp>  // TODO remove once we have component building via RTTI
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
    std::string respath;
    c4::from_chars(def["res"].val(), &respath);
    auto hash = entt::hashed_string(respath.c_str());
    dst.spr = rman().load_sync<rsprite>(hash.value());
    if(def.has_child("color"))
        load_vec4(def["color"], dst.color);
    if(def.has_child("visible"))
        def["visible"] >> dst.visible;
    if(def.has_child("pixel_snap"))
        def["pixel_snap"] >> dst.pixel_snap;
    if(def.has_child("animating"))
        def["animating"] >> dst.animating;
    if(def.has_child("blend"))
        def["blend"] >> ((int&)dst.blend);
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
    dst.script = rman().load_sync<rscript>(hash.value());
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

bool nb::build_tilemap(ryml::ConstNodeRef def, ctilemap &dst)
{
    if (def.has_child("res"))
    {
        std::string respath;
        c4::from_chars(def["res"].val(), &respath);
        dst.map = rman().load_sync<rtilemap>(entt::hashed_string{respath.c_str()}.value());
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

bool nb::build_camera(ryml::ConstNodeRef def, ccamera &dst)
{
    if (def.has_child("2d"))
    {
        const auto &def2d = def["2d"];
        if (def2d.has_child("scale"))   def2d["scale"]   >> dst.cam2d.scale;
        if (def2d.has_child("fit_mode"))  def2d["fit_mode"]  >> reinterpret_cast<int&>(dst.cam2d.fit_mode);
        if (def2d.has_child("fit_dims"))    try_load_vec2(def2d["fit_dims"], dst.cam2d.fit_world_dims);
        if (def2d.has_child("fit_anchor"))    try_load_vec2(def2d["fit_anchor"], dst.cam2d.fit_anchor);
    }
    if (def.has_child("3d"))
    {
        const auto &def3d = def["3d"];
        if (def3d.has_child("clip_near"))   def3d["clip_near"]   >> dst.cam3d.clip_near;
        if (def3d.has_child("clip_far"))   def3d["clip_far"]   >> dst.cam3d.clip_far;
        if (def3d.has_child("fov"))   def3d["fov"]   >> dst.cam3d.fov;
    }
    return true;
}

bool nb::build_textext(ryml::ConstNodeRef def, ctextext &dst)
{
    if (def.has_child("font"))
    {
        std::string respath;
        c4::from_chars(def["font"].val(), &respath);
        dst.font = rman().load_sync<rtexfont>(entt::hashed_string{respath.c_str()}.value());
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

bool nb::build_layers(ryml::ConstNodeRef def, clayers &dst)
{
    if (def.has_child("mask"))
        def["mask"] >> dst.mask;
    return true;
}


bool nb::build_mesh(ryml::ConstNodeRef def, cmesh &dst)
{
    std::string respath;
    c4::from_chars(def["mesh"].val(), &respath);
    rman().note_subresource(respath);
    auto hash = entt::hashed_string(respath.c_str());
    dst.mesh = rman().load_sync<rmesh>(hash.value());
    if(dst.mesh)
    {
        const auto &mat_slots = dst.mesh->material_slots();
        for(uint i = 0; i < mat_slots.size() && i < cmesh::MAX_MATERIALS; i++)
        {
            if(mat_slots[i] != entt::null_t{})
                dst.materials[i] = rman().load_sync<rmaterial>(mat_slots[i]);
        }
    }
    return true;
}
