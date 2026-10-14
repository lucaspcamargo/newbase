#include <newbase/components/mesh.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/log.hpp>
#include <entt/meta/factory.hpp>
#include <IconsForkAwesome.h>

using namespace nb;
using entt::operator""_hs;

void cmesh::_ensure_rtti()
{
    entt::meta_factory<cmesh>{}
    .custom<rtti::type_info>(rtti::type_info{
        .identifier = "mesh",
        .type_class = rtti::TYPE_CLASS_COMPONENT,
        .data {
            .component = {
                .editor_icon = ICON_FK_CUBE
            }
        }
    })
    .ctor<>()
    .data<&cmesh::mesh>("mesh"_hs)
    .custom<rtti::data_info>(rtti::data_info{
        .identifier     = "mesh",
        .subtype        = rtti::DATA_SUBTYPE_RESOURCE,
        .resource_type_id = "rmesh"_hs.value()
    })
    .data<&cmesh::materials>("materials"_hs)
    .custom<rtti::data_info>(rtti::data_info{
        .identifier     = "materials",
        .subtype        = rtti::DATA_SUBTYPE_RESOURCE,
        .resource_type_id = "rmaterial"_hs.value()
    });
    log::info("[cmesh] registered");
}
