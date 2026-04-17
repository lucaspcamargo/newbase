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
        .data<&ccamera::zoom>("zoom"_hs)
            .custom<rtti::data_info>(rtti::data_info{ "zoom" })
        .data<&ccamera::near_z>("near_z"_hs)
            .custom<rtti::data_info>(rtti::data_info{ "near_z" })
        .data<&ccamera::far_z>("far_z"_hs)
            .custom<rtti::data_info>(rtti::data_info{ "far_z" })
        .data<&ccamera::wmax>("wmax"_hs)
            .custom<rtti::data_info>(rtti::data_info{ "wmax" })
        .data<&ccamera::hmax>("hmax"_hs)
            .custom<rtti::data_info>(rtti::data_info{ "hmax" });
    log::info("[ccamera] registered");
}
