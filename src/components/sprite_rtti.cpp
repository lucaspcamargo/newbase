#include <newbase/components/sprite.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/log.hpp>
#include <entt/meta/factory.hpp>
#include <IconsForkAwesome.h>

using namespace nb;
using entt::operator""_hs;

void csprite::_ensure_rtti()
{
    entt::meta_factory<csprite>{}
        .custom<rtti::type_info>(rtti::type_info{
            .identifier = "sprite",
            .type_class = rtti::TYPE_CLASS_COMPONENT,
            .data {
                .component = {
                    .editor_icon = ICON_FK_PICTURE_O
                }
            }
        })
        .ctor<>()
        .data<&csprite::spr>("spr"_hs)
            .custom<rtti::data_info>(rtti::data_info{
                .identifier       = "spr",
                .subtype          = rtti::DATA_SUBTYPE_RESOURCE,
                .resource_type_id = "rsprite"_hs.value()
            })
        .data<&csprite::visible>("visible"_hs)
            .custom<rtti::data_info>(rtti::data_info{"visible"})
        .data<&csprite::pixel_snap>("pixel_snap"_hs)
            .custom<rtti::data_info>(rtti::data_info{"pixel_snap"})
        .data<&csprite::color>("color"_hs)
            .custom<rtti::data_info>(rtti::data_info{.identifier="color", .subtype=rtti::DATA_SUBTYPE_COLOR})
        .data<&csprite::sequence>("sequence"_hs)
            .custom<rtti::data_info>(rtti::data_info{"sequence"})
        .data<&csprite::frame>("frame"_hs)
            .custom<rtti::data_info>(rtti::data_info{"frame"})
        .data<&csprite::animating>("animating"_hs)
            .custom<rtti::data_info>(rtti::data_info{"animating"});
    log::info("[csprite] registered: id=%x");
}

