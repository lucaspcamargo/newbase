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
        .ctor<>()
        .data<&cscript::script>("script"_hs)
            .custom<rtti::data_info>(rtti::data_info{
                .identifier       = "script",
                .subtype          = rtti::DATA_SUBTYPE_RESOURCE,
                .resource_type_id = "rscript"_hs.value()
            });
    log::info("[cscript] registered: id=%x");
}

