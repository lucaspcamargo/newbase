#include <newbase/components/script.h>
#include <newbase/reflection/data.h>
#include <newbase/log.h>
#include <entt/meta/factory.hpp>
#include <sol/sol.hpp>
#include <IconsForkAwesome.h>

using namespace nb;
using entt::operator""_hs;

static rtti::component_type_info::bind_result _cscript_bind(void *state)
{
    sol::state_view lua{reinterpret_cast<lua_State*>(state)};
    auto ut = lua.new_usertype<cscript>("cscript");

    return rtti::component_type_info::bind_result{ "cscript", [](void *state, entt::entity id, entt::registry &reg){
        sol::state_view lua{reinterpret_cast<lua_State*>(state)};
        lua.set_function("script", [id, &reg]() -> cscript* {
            return &(reg.get<cscript>(id));
        });
    }};
}

void cscript::_ensure_rtti()
{
    entt::meta_factory<cscript>{}
        .custom<rtti::component_type_info>(rtti::component_type_info{
            "script", 
            true,
            &_cscript_bind,
            ICON_FK_FILE_CODE_O
        })
        .ctor<>();
    log::info("[cscript] registered: id=%x");
}

