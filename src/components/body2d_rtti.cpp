#include <newbase/components/body2d.h>
#include <newbase/reflection/data.h>
#include <newbase/log.h>
#include <entt/meta/factory.hpp>
#include <sol/sol.hpp>
#include <IconsForkAwesome.h>

using namespace nb;
using entt::operator""_hs;

static rtti::component_type_info::bind_result _cbody2d_bind(void *state)
{
    sol::state_view lua{reinterpret_cast<lua_State*>(state)};
    auto ut = lua.new_usertype<cbody2d>("cbody2d");

    return rtti::component_type_info::bind_result{ "cbody2d", [](void *envp, entt::entity id, entt::registry &reg){
        sol::environment &env = *reinterpret_cast<sol::environment*>(envp);
        env.set_function("body2d", [id, &reg]() -> cbody2d* {
            return &(reg.get<cbody2d>(id));
        });
    }};
}

void cbody2d::_ensure_rtti()
{
    // already registered
    if(entt::resolve<cbody2d>().info() == entt::type_id<void>())
        return;

    entt::meta_factory<cbody2d>{}
        .custom<rtti::component_type_info>(rtti::component_type_info{
            "body2d",
            true,
            &_cbody2d_bind,
            ICON_FK_SQUARE_O
        })
        .ctor<>();
    log::info("[cbody2d] registered: id=%x");
}

