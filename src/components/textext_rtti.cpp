#include <newbase/components/textext.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/log.hpp>
#include <entt/meta/factory.hpp>
#include <IconsForkAwesome.h>

using namespace nb;
using entt::operator""_hs;

void ctextext::_ensure_rtti()
{
    entt::meta_factory<ctextext>{}
        .custom<rtti::type_info>(rtti::type_info{
            .identifier = "textext",
            .type_class = rtti::TYPE_CLASS_COMPONENT,
            .data {
                .component = {
                    .editor_icon = ICON_FK_FONT,
                    .notify = [](entt::registry &, entt::entity, void* comp) {
                        if (comp) static_cast<ctextext*>(comp)->dirty = true;
                    }
                }
            }
        })
        .ctor<>()
        .data<&ctextext::font>("font"_hs)
            .custom<rtti::data_info>(rtti::data_info{
                .identifier       = "font",
                .subtype          = rtti::DATA_SUBTYPE_RESOURCE,
                .resource_type_id = "rtexfont"_hs.value()
            })
        .data<&ctextext::text>("text"_hs)
            .custom<rtti::data_info>(rtti::data_info{"text"})
        .data<&ctextext::color, entt::as_ref_t>("color"_hs)
            .custom<rtti::data_info>(rtti::data_info{
                .identifier = "color",
                .subtype    = rtti::DATA_SUBTYPE_COLOR
            });
    log::info("[ctextext] registered");
}
