#include <newbase/scene.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/res/etree.hpp>
#include <newbase/components/builders.hpp>
#include <newbase/components/spatial.hpp>
#include <newbase/components/sprite.hpp>
#include <newbase/components/script.hpp>
#include <newbase/components/body2d.hpp>
#include <newbase/components/mesh2d.hpp>
#include <newbase/geom/geometry_buffer_2d.hpp>
#include <newbase/log.hpp>

#include <entt/entt.hpp>
#include <ryml_std.hpp>
#include <SDL3/SDL.h>

struct nb::scene_p {
    entt::id_type id;
    entt::registry reg;
    std::vector<entt::entity> pending_destroy;
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

entt::entity nb::scene::build_etree(entt::id_type retree_id, entt::id_type parent)
{
    (void) parent; // TODO hierarchy stuff
    auto &reg = _d->reg;
    auto res = rman().get<retree>(retree_id, false);
    if(!res)
    {
        log::warn("[scene] build_etree: cannot load: %x", static_cast<uint32_t>(retree_id));
        return entt::null;
    }

    if(!res->etree_valid)
    {
        log::warn("[scene] build_etree: invalid etree: %x", static_cast<uint32_t>(retree_id));
        return entt::null;
    }

    log::info("[scene] build_etree: %x", static_cast<uint32_t>(retree_id));

    entt::entity first = entt::null;
    for(auto ent: res->tree.rootref())
    {
        std::string entname;
        c4::from_chars(ent["name"].val(), &entname);
        auto eid = reg.create();
        if(first == entt::null) first = eid;

        log::info("[scene] build_etree: ent %s (%x)", entname.c_str(), static_cast<uint32_t>(eid));
        for(auto comp: ent["comps"])
        {
            std::string compname;
            c4::from_chars(comp.key(), &compname);
            log::info("[scene] build_etree: comp %s", compname.c_str());
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
            else if(compname == "body2d")
            {
                auto &s = reg.emplace<nb::cbody2d>(eid);
                nb::build_body2d(comp, s);
            }
            else
            {
                log::warn("[scene] build_etree: unknown component '%s' on entity '%s'", compname.c_str(), entname.c_str());
            }
        }
    }
    // TODO: remove — temporary cmesh2d smoke-test entity
    {
        auto eid = reg.create();
        auto& sp = reg.emplace<nb::cspatial>(eid);
        sp.pos = {0.f, 0.f, 5.f};
        sp.apply();

        auto geom = std::make_shared<nb::geometry_buffer_2d>();
        geom->vertices.push_back({{   0.f, -100.f}, {0.5f, 0.f}, {1.f, 0.2f, 0.2f, 1.f}});
        geom->vertices.push_back({{ 100.f,  100.f}, {1.f,  1.f}, {0.2f, 1.f, 0.2f, 1.f}});
        geom->vertices.push_back({{-100.f,  100.f}, {0.f,  1.f}, {0.2f, 0.2f, 1.f, 1.f}});

        auto& mesh = reg.emplace<nb::cmesh2d>(eid);
        mesh.geom = geom;
    }

    return first;
}

void nb::scene::clear()
{
    _d->pending_destroy.clear();
    _d->reg.clear();
}

void nb::scene::queue_destroy(entt::entity e)
{
    _d->pending_destroy.push_back(e);
}

void nb::scene::flush_destroy_queue()
{
    for(auto e : _d->pending_destroy)
    {
        if(_d->reg.valid(e))
            _d->reg.destroy(e);
    }
    _d->pending_destroy.clear();
}
