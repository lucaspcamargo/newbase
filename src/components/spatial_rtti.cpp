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
            .data {
                .component {
                    .editor_icon = ICON_FK_ARROWS,
                    .notify = [](entt::registry &r, entt::entity e, void* comp) {
                        if(comp) static_cast<cspatial*>(comp)->apply();
                    }
                }
            },
            .make_ref_any = &rtti::make_component_ref_any<cspatial>
        })
        .ctor<>()
        .data<&cspatial::set_pos, &cspatial::pos>("pos"_hs)
            .custom<rtti::data_info>(rtti::data_info{ "pos" })
        .data<&cspatial::set_rot, &cspatial::rot>("rot"_hs)
            .custom<rtti::data_info>(rtti::data_info{ "rot" })
        .func<&cspatial::clear>("clear"_hs)
            .custom<rtti::func_info>(rtti::func_info{"clear"})
        .func<&cspatial::apply>("apply"_hs)
            .custom<rtti::func_info>(rtti::func_info{"apply"});
    
    log::info("[cspatial] registered: id=%x");
}

