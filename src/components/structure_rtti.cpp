#include <newbase/components/structure.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/log.hpp>
#include <entt/meta/factory.hpp>
#include <IconsForkAwesome.h>

using namespace nb;
using entt::operator""_hs;

static std::string _cstructure_get_name(cstructure &self) { return self.get_name(); }
static void _cstructure_set_name(cstructure &self, std::string s) { self.set_name(std::move(s)); }

void cstructure::_ensure_rtti()
{
    entt::meta_factory<cstructure>{}
        .custom<rtti::type_info>(rtti::type_info{
            .identifier = "structure",
            .type_class = rtti::TYPE_CLASS_COMPONENT,
            .data {
                .component {
                    .editor_icon = ICON_FK_SITEMAP,
                }
            }
        })
        .ctor<>()
        .func<&_cstructure_get_name>("get_name"_hs)
            .custom<rtti::func_info>(rtti::func_info{"get_name"})
        .func<&_cstructure_set_name>("set_name"_hs)
            .custom<rtti::func_info>(rtti::func_info{"set_name"});

    log::info("[cstructure] registered");
}
