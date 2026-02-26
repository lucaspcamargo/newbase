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
        .custom<rtti::type_info>(rtti::type_info{
            .identifier = "script", 
            .type_class = rtti::TYPE_CLASS_COMPONENT,
            .data {
                .component = {
                    .editor_icon = ICON_FK_FILE_CODE_O
                }
            }
        })
        .ctor<>();
    log::info("[cscript] registered: id=%x");
}

