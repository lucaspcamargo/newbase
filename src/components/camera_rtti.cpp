#include <newbase/components/camera.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/log.hpp>
#include <entt/meta/factory.hpp>
#include <IconsForkAwesome.h>

using namespace nb;
using entt::operator""_hs;

void ccamera::_ensure_rtti()
{
    entt::meta_factory<ccamera>{}
        .custom<rtti::type_info>(rtti::type_info{
            .identifier = "camera",
            .type_class = rtti::TYPE_CLASS_COMPONENT,
            .data {
                .component = {
                    .editor_icon = ICON_FK_VIDEO_CAMERA
                }
            }
        })
        .ctor<>()
        .data<&ccamera::cam2d, entt::as_ref_t>("cam2d"_hs)
            .custom<rtti::data_info>(rtti::data_info{ "cam2d" })
        .data<&ccamera::cam3d, entt::as_ref_t>("cam3d"_hs)
            .custom<rtti::data_info>(rtti::data_info{ "cam3d" });
    log::info("[ccamera] registered");
}
