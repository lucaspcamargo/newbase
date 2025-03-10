#include <newbase/ecs.h>
#include <newbase/res/manager.h>
#include <newbase/res/etree.h>
#include <newbase/components/builders.h>
#include <newbase/components/spatial.h>
#include <newbase/components/sprite.h>
#include <newbase/res/etree.h>

#include <entt/entt.hpp>
#include <ryml_std.hpp>
#include <SDL3/SDL.h>

static entt::registry _reg;

entt::registry &nb::reg()
{
    return _reg;
}

entt::id_type nb::build_etree(entt::id_type retree_id)
{
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
        auto eid = _reg.create();

        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[ecs] build_etree: ent %s", entname.c_str());
        for(auto comp: ent["comps"])
        {
            std::string compname;
            c4::from_chars(comp.key(), &compname);
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[ecs] build_etree: comp %s", compname.c_str());
            if(compname == "spatial")
            {
                auto &s = _reg.emplace<nb::cspatial>(eid);
                nb::build_spatial(comp, s);
            }
            else if(compname == "sprite")
            {
                auto &s = _reg.emplace<nb::csprite>(eid);
                nb::build_sprite(comp, s);
            }
            else assert(0);
        }
    }
    // TODO
    return entt::tombstone;
}