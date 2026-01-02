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
        .custom<rtti::component_type_info>(rtti::component_type_info{
            "spatial", 
            true, 
            nullptr,
            ICON_FK_ARROWS
        })
        .ctor<>()
        .data<&cspatial::pos>("pos"_hs)
        .custom<rtti::data_info>(rtti::data_info{"pos"});
    log::info("[cspatial] registered: id=%x");
}

