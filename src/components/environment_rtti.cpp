#include "entt/meta/policy.hpp"
#include <newbase/components/environment.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/log.hpp>
#include <entt/meta/factory.hpp>
#include <IconsForkAwesome.h>

using namespace nb;
using entt::operator""_hs;

void cenvironment::_ensure_rtti()
{
    entt::meta_factory<cenvironment>{}
        .custom<rtti::type_info>(rtti::type_info{
            .identifier = "environment",
            .type_class = rtti::TYPE_CLASS_COMPONENT,
            .data {
                .component = {
                    .editor_icon = ICON_FK_GLOBE
                }
            }
        })
        .ctor<>()
        .data<&cenvironment::equirectangular>("equirectangular"_hs)
            .custom<rtti::data_info>(rtti::data_info{
                .identifier = "equirectangular",
                .subtype = rtti::DATA_SUBTYPE_RESOURCE,
                .resource_type_id = "rtexture"_hs.value()
            })
        .data<&cenvironment::ambient, entt::as_ref_t>("ambient"_hs)
        .custom<rtti::data_info>(rtti::data_info{.identifier = "ambient", .subtype=rtti::DATA_SUBTYPE_COLOR});
    log::info("[cenvironment] registered");
}
