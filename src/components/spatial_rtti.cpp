#include <newbase/components/spatial.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/log.hpp>
#include <entt/meta/factory.hpp>
#include <sol/sol.hpp>
#include <IconsForkAwesome.h>

using namespace nb;
using entt::operator""_hs;

static rtti::component_type_info::bind_result _cspatial_bind(void *state)
{
    sol::state_view lua{reinterpret_cast<lua_State*>(state)};
    auto ut = lua.new_usertype<cspatial>("cspatial");
    ut["pos"] = &cspatial::pos;
    ut["rot"] = &cspatial::rot;
    ut["scale"] = &cspatial::scale;
    ut["clear"] = &cspatial::clear;
    ut["apply"] = [](cspatial &spatial){spatial.apply();};
    

    return rtti::component_type_info::bind_result{ "cspatial", [](void *envp, entt::entity id, entt::registry &reg){
        sol::environment &env = *reinterpret_cast<sol::environment*>(envp);
        env.set_function("spatial", [id, &reg]() -> cspatial* {
            return &(reg.get<cspatial>(id));
        });
    }};
}

void cspatial::_ensure_rtti()
{
    entt::meta_factory<cspatial>{}
        .custom<rtti::component_type_info>(rtti::component_type_info{
            "spatial", 
            true, 
            &_cspatial_bind,
            ICON_FK_ARROWS
        })
        .ctor<>()
        .data<&cspatial::pos>("pos"_hs)
        .custom<rtti::data_info>(rtti::data_info{"pos"});
    log::info("[cspatial] registered: id=%x");
}

