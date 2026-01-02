#include <newbase/components/script.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/log.hpp>
#include <entt/meta/factory.hpp>
#include <IconsForkAwesome.h>

using namespace nb;
using entt::operator""_hs;

void cscript::_ensure_rtti()
{
    entt::meta_factory<cscript>{}
        .custom<rtti::component_type_info>(rtti::component_type_info{
            "script", 
            true,
            nullptr,
            ICON_FK_FILE_CODE_O
        })
        .ctor<>();
    log::info("[cscript] registered: id=%x");
}

