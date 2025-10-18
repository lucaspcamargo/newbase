#include <newbase/components/script.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/log.hpp>
#include <entt/meta/factory.hpp>
#include <sol/sol.hpp>
#include <IconsForkAwesome.h>

using namespace nb;
using entt::operator""_hs;

static rtti::component_type_info::bind_result _cscript_bind(void *state)
{
    sol::state_view lua{reinterpret_cast<lua_State*>(state)};
    auto ut = lua.new_usertype<cscript>("cscript");

    return rtti::component_type_info::bind_result{ "cscript", [](void *envp, entt::entity id, entt::registry &reg){
        sol::environment &env = *reinterpret_cast<sol::environment*>(envp);
        env.set_function("script", [id, &reg]() -> cscript* {
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

