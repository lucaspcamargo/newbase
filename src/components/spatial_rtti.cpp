#include <newbase/components/spatial.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/log.hpp>
#include <entt/meta/factory.hpp>
#include <IconsForkAwesome.h>

using namespace nb;
using entt::operator""_hs;

void cspatial::_ensure_rtti()
{
    entt::meta_factory<cspatial>{}
        .custom<rtti::type_info>(rtti::type_info{
            .identifier = "spatial",
            .type_class = rtti::TYPE_CLASS_COMPONENT,
            .data{ .component {.editor_icon = ICON_FK_ARROWS} }
        })
        .ctor<>()
        .data<&cspatial::pos>("pos"_hs);
    log::info("[cspatial] registered: id=%x");
}

