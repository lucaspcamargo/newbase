#include <newbase/components/particle_emitter.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/log.hpp>
#include <entt/meta/factory.hpp>
#include <IconsForkAwesome.h>

using namespace nb;
using entt::operator""_hs;

void cparticle_emitter::_ensure_rtti()
{
    entt::meta_factory<cparticle_emitter>{}
        .custom<rtti::type_info>(rtti::type_info{
            .identifier = "particle_emitter",
            .type_class = rtti::TYPE_CLASS_COMPONENT,
            .data {
                .component = {
                    .editor_icon = ICON_FK_STAR_O
                }
            }
        })
        .ctor<>()
        .data<&cparticle_emitter::res>("res"_hs)
            .custom<rtti::data_info>(rtti::data_info{
                .identifier       = "res",
                .subtype          = rtti::DATA_SUBTYPE_RESOURCE,
                .resource_type_id = "rparticle_emitter"_hs.value()
            })
        .data<&cparticle_emitter::emitting>("emitting"_hs)
            .custom<rtti::data_info>(rtti::data_info{"emitting"})
        .data<&cparticle_emitter::emit_rate_override>("emit_rate_override"_hs)
            .custom<rtti::data_info>(rtti::data_info{"emit_rate_override"});
    log::info("[cparticle_emitter] registered");
}
