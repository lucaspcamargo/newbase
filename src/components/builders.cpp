#include <newbase/components/builders.h>
#include <newbase/components/spatial.h>
#include <newbase/components/sprite.h>
#include <newbase/components/script.h>
#include <newbase/res/manager.h>
#include <newbase/yaml/glm.h>

#include <entt/entt.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <ryml.hpp>
#include <ryml_std.hpp>


using namespace nb;

bool ::nb::build_spatial(ryml::ConstNodeRef def, cspatial &dst)
{
    load_vec3(def["pos"], dst.pos);
    load_vec3(def["rot"], dst.rot);
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
    c4::from_chars(def["res"].val(), &respath);
    auto hash = entt::hashed_string(respath.c_str());
    dst.script = rman().get_script(hash.value());
    return true;
}
