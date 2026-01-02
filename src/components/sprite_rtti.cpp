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
        .custom<rtti::component_type_info>(rtti::component_type_info{
            "sprite",
            true,
            nullptr,
            ICON_FK_PICTURE_O
        })
        .ctor<>();
    log::info("[csprite] registered: id=%x");
}

