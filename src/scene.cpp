#include <newbase/scene.h>
#include <newbase/res/manager.h>
#include <newbase/res/etree.h>
#include <newbase/components/builders.h>
#include <newbase/components/spatial.h>
#include <newbase/components/sprite.h>
#include <newbase/components/script.h>
#include <newbase/res/etree.h>

#include <entt/entt.hpp>
#include <ryml_std.hpp>
#include <SDL3/SDL.h>

struct nb::scene_p {
    entt::id_type id;
    entt::registry reg;
};


nb::scene::scene(entt::id_type scene_id) : nocopy()
{
    _d = new nb::scene_p();
    _d->id = scene_id;
}

nb::scene::~scene()
{
    _d->reg.clear();
    delete _d;
}

entt::registry& nb::scene::registry()
{
    return _d->reg;
}

entt::id_type nb::scene::build_etree(entt::id_type retree_id, entt::id_type parent)
{
    (void) parent; // TODO hirerarchy stuff
    auto &reg = _d->reg;
    auto res = rman().get_etree(retree_id, false);
    if(!res)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[ecs] build_etree: cannot load: %x", static_cast<uint32_t>(retree_id));
        return entt::tombstone;
    }

    if(!res->valid)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[ecs] build_etree: invalid etree: %x", static_cast<uint32_t>(retree_id));
        return entt::tombstone;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[ecs] build_etree: %x", static_cast<uint32_t>(retree_id));

    for(auto ent: res->tree.rootref())
    {
        std::string entname;
        c4::from_chars(ent["name"].val(), &entname);
        auto eid = reg.create();

        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[ecs] build_etree: ent %s", entname.c_str());
        for(auto comp: ent["comps"])
        {
            std::string compname;
            c4::from_chars(comp.key(), &compname);
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[ecs] build_etree: comp %s", compname.c_str());
            if(compname == "spatial")
            {
                auto &s = reg.emplace<nb::cspatial>(eid);
                nb::build_spatial(comp, s);
            }
            else if(compname == "sprite")
            {
                auto &s = reg.emplace<nb::csprite>(eid);
                nb::build_sprite(comp, s);
            }
            else if(compname == "script")
            {
                auto &s = reg.emplace<nb::cscript>(eid);
                nb::build_script(comp, s);
            }
            else assert(0);
        }
    }
    // TODO
    return entt::tombstone;
}
