#include <newbase/components/mesh2d.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/log.hpp>
#include <entt/meta/factory.hpp>
#include <IconsForkAwesome.h>

using namespace nb;
using entt::operator""_hs;

void cmesh2d::_ensure_rtti()
{
    entt::meta_factory<cmesh2d>{}
        .custom<rtti::type_info>(rtti::type_info{
            .identifier = "mesh2d",
            .type_class = rtti::TYPE_CLASS_COMPONENT,
            .data {
                .component = {
                    .editor_icon = ICON_FK_OBJECT_GROUP
                }
            }
        })
        .ctor<>()
        .data<&cmesh2d::tex>("tex"_hs)
            .custom<rtti::data_info>(rtti::data_info{
                .identifier     = "tex",
                .subtype        = rtti::DATA_SUBTYPE_RESOURCE,
                .resource_type_id = "rtexture"_hs.value()
            })
        .data<&cmesh2d::visible>("visible"_hs)
            .custom<rtti::data_info>(rtti::data_info{"visible"});
    log::info("[cmesh2d] registered");
}
