#include <newbase/components/spatial.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/log.hpp>
#include <entt/meta/factory.hpp>
#include <IconsForkAwesome.h>

using namespace nb;
using entt::operator""_hs;

static void _cspatial_apply(cspatial &self) { self.apply(); }

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
            }
        })
        .ctor<>()
        .data<&cspatial::pos>("pos"_hs)
            .custom<rtti::data_info>(rtti::data_info{ "pos" })
        .data<&cspatial::rot>("rot"_hs)
            .custom<rtti::data_info>(rtti::data_info{ "rot" })
        .func<&cspatial::clear>("clear"_hs)
            .custom<rtti::func_info>(rtti::func_info{"clear"})
        .func<&_cspatial_apply>("apply"_hs)
            .custom<rtti::func_info>(rtti::func_info{"apply"});
    
    log::info("[cspatial] registered: id=%x");
}

