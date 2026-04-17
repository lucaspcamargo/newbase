#include <newbase/components/layers.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/log.hpp>
#include <entt/meta/factory.hpp>
#include <IconsForkAwesome.h>

using namespace nb;
using entt::operator""_hs;

void clayers::_ensure_rtti()
{
    entt::meta_factory<clayers>{}
        .custom<rtti::type_info>(rtti::type_info{
            .identifier = "layers",
            .type_class = rtti::TYPE_CLASS_COMPONENT,
            .data {
                .component = {
                    .editor_icon = ICON_FK_OBJECT_GROUP
                }
            }
        })
        .ctor<>()
        .data<&clayers::mask>("mask"_hs)
            .custom<rtti::data_info>(rtti::data_info{ "mask" });
    log::info("[clayers] registered");
}
