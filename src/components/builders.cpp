#include <newbase/components/builders.h>
#include <newbase/components/spatial.h>
#include <newbase/components/sprite.h>
#include <newbase/components/script.h>
#include <newbase/components/body2d.h>
#include <newbase/res/manager.h>
#include <newbase/yaml/glm.h>
#include <newbase/log.h>

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
    dst.spr = rman().get_sprite(hash.value());
    return true;
}

bool ::nb::build_script(ryml::ConstNodeRef def, cscript &dst)
{
    // TODO should components have resource handles or just ids?
    std::string respath;
    c4::from_chars(def["lua"].val(), &respath);
    auto hash = entt::hashed_string(respath.c_str());
    dst.script = rman().get_script(hash.value());
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
    {
        def["gravity_scale"] >> dst.gravity_scale;
    }

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

            dst.shapes.push_back(shape);
        }
    }
        

    return true;
}