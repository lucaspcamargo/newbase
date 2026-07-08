#include <newbase/components/character2d.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/log.hpp>
#include <entt/meta/factory.hpp>
#include <entt/entity/registry.hpp>
#include <IconsForkAwesome.h>

using namespace nb;
using entt::operator""_hs;

void ccharacter2d::_ensure_rtti()
{
    if(entt::resolve<ccharacter2d>().info() == entt::type_id<void>())
        return;

    entt::meta_factory<ccharacter2d>{}
        .custom<rtti::type_info>(rtti::type_info{
            .identifier = "character2d",
            .type_class = rtti::TYPE_CLASS_COMPONENT,
            .data {
                .component = {
                    .editor_icon = ICON_FK_MALE,
                }
            }
        })
        .ctor<>()
        .data<&ccharacter2d::capsule_radius>("capsule_radius"_hs)
            .custom<rtti::data_info>(rtti::data_info{"capsule_radius"})
        .data<&ccharacter2d::capsule_half_height>("capsule_half_height"_hs)
            .custom<rtti::data_info>(rtti::data_info{"capsule_half_height"})
        .data<&ccharacter2d::velocity, entt::as_ref_t>("velocity"_hs)
            .custom<rtti::data_info>(rtti::data_info{"velocity"})
        .data<&ccharacter2d::gravity_scale>("gravity_scale"_hs)
            .custom<rtti::data_info>(rtti::data_info{"gravity_scale"})
        .data<&ccharacter2d::grounded>("grounded"_hs)
            .custom<rtti::data_info>(rtti::data_info{"grounded"})
        .data<&ccharacter2d::category_bits>("category_bits"_hs)
            .custom<rtti::data_info>(rtti::data_info{"category_bits"})
        .data<&ccharacter2d::mask_bits>("mask_bits"_hs)
            .custom<rtti::data_info>(rtti::data_info{"mask_bits"})
        .data<&ccharacter2d::push_force>("push_force"_hs)
            .custom<rtti::data_info>(rtti::data_info{"push_force"})
        .data<&ccharacter2d::on_ladder>("on_ladder"_hs)
            .custom<rtti::data_info>(rtti::data_info{"on_ladder"});
    log::info("[ccharacter2d] registered");
}
