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
            .custom<rtti::data_info>(rtti::data_info{"spr"});
    log::info("[csprite] registered: id=%x");
}

