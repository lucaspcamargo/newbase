#include <newbase/components/tilemap.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/log.hpp>
#include <entt/meta/factory.hpp>
#include <IconsForkAwesome.h>

using namespace nb;
using entt::operator""_hs;

void ctilemap::_ensure_rtti()
{
    entt::meta_factory<ctilemap>{}
        .custom<rtti::type_info>(rtti::type_info{
            .identifier = "tilemap",
            .type_class = rtti::TYPE_CLASS_COMPONENT,
            .data {
                .component = {
                    .editor_icon = ICON_FK_MAP
                }
            }
        })
        .ctor<>()
        .data<&ctilemap::map>("map"_hs)
            .custom<rtti::data_info>(rtti::data_info{
                .identifier       = "map",
                .subtype          = rtti::DATA_SUBTYPE_RESOURCE,
                .resource_type_id = "rtilemap"_hs.value()
            })
        .data<&ctilemap::collision_layer>("collision_layer"_hs)
            .custom<rtti::data_info>(rtti::data_info{"collision_layer"})
        .data<&ctilemap::visible>("visible"_hs)
            .custom<rtti::data_info>(rtti::data_info{"visible"});
    log::info("[ctilemap] registered");
}
