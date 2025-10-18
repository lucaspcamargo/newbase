#include <newbase/components/body2d.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/log.hpp>
#include <entt/meta/factory.hpp>
#include <sol/sol.hpp>
#include <IconsForkAwesome.h>

using namespace nb;
using entt::operator""_hs;

static rtti::component_type_info::bind_result _cbody2d_bind(void *state)
{
    sol::state_view lua{reinterpret_cast<lua_State*>(state)};

    auto uts = lua.new_usertype<shape2d>("shape2d");
    uts["shape_type"] = &shape2d::shape_type;
    uts["shape_data"] = &shape2d::shape_data;
    uts["density"] = &shape2d::density;
    uts["friction"] = &shape2d::friction;
    uts["restitution"] = &shape2d::restitution;
    uts["rolling_resistance"] = &shape2d::rolling_resistance;
    uts["tangent_speed"] = &shape2d::tangent_speed;
    uts["category_bits"] = &shape2d::category_bits;
    uts["mask_bits"] = &shape2d::mask_bits;
    uts["group"] = &shape2d::group;
    uts["sensor"] = &shape2d::sensor;
    uts["sensor_events"] = &shape2d::sensor_events;

    auto ut = lua.new_usertype<cbody2d>("cbody2d");
    ut["linear_damping"] = &cbody2d::linear_damping;
    ut["angular_damping"] = &cbody2d::angular_damping;
    ut["gravity_scale"] = &cbody2d::gravity_scale;
    ut["enabled"] = &cbody2d::enabled;
    ut["enable_sleep"] = &cbody2d::enable_sleep;
    ut["awake"] = &cbody2d::awake;
    ut["fix_rotation"] = &cbody2d::fix_rotation;
    ut["bullet"] = &cbody2d::bullet;
    ut["shapes"] = &cbody2d::shapes;
    ut["dirty"] = &cbody2d::dirty;


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

