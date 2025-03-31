#include <newbase/components/sprite.h>
#include <newbase/reflection/data.h>
#include <newbase/log.h>
#include <entt/meta/factory.hpp>
#include <sol/sol.hpp>
#include <IconsForkAwesome.h>

using namespace nb;
using entt::operator""_hs;

static rtti::component_type_info::bind_result _csprite_bind(void *state)
{
    sol::state_view lua{reinterpret_cast<lua_State*>(state)};
    auto ut = lua.new_usertype<csprite>("csprite");
    
    return rtti::component_type_info::bind_result{ "csprite", [](void *state, void *data){
        sol::state_view lua{reinterpret_cast<lua_State*>(state)};
        lua["sprite"] = reinterpret_cast<csprite*>(data);
    }};
}

void csprite::_ensure_rtti()
{
    entt::meta_factory<csprite>{}
        .custom<rtti::component_type_info>(rtti::component_type_info{
            "sprite",
            true,
            &_csprite_bind,
            ICON_FK_PICTURE_O
        })
        .ctor<>();
    log::info("[csprite] registered: id=%x");
}

