#include <newbase/components/body2d.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/log.hpp>
#include <entt/meta/factory.hpp>
#include <entt/entity/registry.hpp>
#include <IconsForkAwesome.h>

using namespace nb;
using entt::operator""_hs;


void cbody2d::_ensure_rtti()
{
    // already registered
    if(entt::resolve<cbody2d>().info() == entt::type_id<void>())
        return;

    entt::meta_factory<cbody2d>{}
        .custom<rtti::type_info>(rtti::type_info{
            .identifier = "body2d",
            .type_class = rtti::TYPE_CLASS_COMPONENT,
            .data {
                .component = {
                    .editor_icon = ICON_FK_SQUARE_O,
                    .notify = [](entt::registry &r, entt::entity e) {
                        r.patch<cbody2d>(e, [](auto&){});
                    }
                }
            }
        })
        .ctor<>()
        .data<&cbody2d::linear_damping>("linear_damping"_hs)
            .custom<rtti::data_info>(rtti::data_info{"linear_damping"})
        .data<&cbody2d::angular_damping>("angular_damping"_hs)
            .custom<rtti::data_info>(rtti::data_info{"angular_damping"})
        .data<&cbody2d::gravity_scale>("gravity_scale"_hs)
            .custom<rtti::data_info>(rtti::data_info{"gravity_scale"})
        .data<&cbody2d::enabled>("enabled"_hs)
            .custom<rtti::data_info>(rtti::data_info{"enabled"})
        .data<&cbody2d::enable_sleep>("enable_sleep"_hs)
            .custom<rtti::data_info>(rtti::data_info{"enable_sleep"})
        .data<&cbody2d::awake>("awake"_hs)
            .custom<rtti::data_info>(rtti::data_info{"awake"})
        .data<&cbody2d::fix_rotation>("fix_rotation"_hs)
            .custom<rtti::data_info>(rtti::data_info{"fix_rotation"})
        .data<&cbody2d::bullet>("bullet"_hs)
            .custom<rtti::data_info>(rtti::data_info{"bullet"});
    log::info("[cbody2d] registered: id=%x");
}

