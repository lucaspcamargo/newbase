#include <newbase/components/light.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/log.hpp>
#include <entt/meta/factory.hpp>
#include <IconsForkAwesome.h>

using namespace nb;
using entt::operator""_hs;

void clight::_ensure_rtti()
{
    entt::meta_factory<clight>{}
    .custom<rtti::type_info>(rtti::type_info{
        .identifier = "light",
        .type_class = rtti::TYPE_CLASS_COMPONENT,
        .data {
            .component = {
                .editor_icon = ICON_FK_LIGHTBULB_O
            }
        }
    })
    .ctor<>()
    .data<&clight::color, entt::as_ref_t>("color"_hs)
    .custom<rtti::data_info>(rtti::data_info{ "color", rtti::DATA_SUBTYPE_COLOR  })
    .data<&clight::intensity, entt::as_ref_t>("intensity"_hs)
    .custom<rtti::data_info>(rtti::data_info{ "intensity" })
    .data<&clight::range, entt::as_ref_t>("range"_hs)
    .custom<rtti::data_info>(rtti::data_info{ "range" })
    .data<&clight::linear, entt::as_ref_t>("linear"_hs)
    .custom<rtti::data_info>(rtti::data_info{ "linear" })
    .data<&clight::quadratic, entt::as_ref_t>("quadratic"_hs)
    .custom<rtti::data_info>(rtti::data_info{ "quadratic" })
    .data<&clight::inner_cone_angle, entt::as_ref_t>("inner_cone_angle"_hs)
    .custom<rtti::data_info>(rtti::data_info{ "inner_cone_angle" })
    .data<&clight::outer_cone_angle, entt::as_ref_t>("outer_cone_angle"_hs)
    .custom<rtti::data_info>(rtti::data_info{ "outer_cone_angle" })
    .data<&clight::casts_shadows, entt::as_ref_t>("casts_shadows"_hs)
    .custom<rtti::data_info>(rtti::data_info{ "casts_shadows" });

    log::info("[clight] registered");
}
