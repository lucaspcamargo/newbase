#include "entt/core/hashed_string.hpp"
#include <newbase/sys/render_gpu/render_gpu.hpp>
#include <newbase/reflection/contexts.hpp>
#include <newbase/reflection/data.hpp>


using namespace nb;
using entt::operator""_hs;


extern "C" void _rtti_init_render_gpu()
{
    entt::meta_factory<nb::render_gpu>{}
    .type("render_gpu"_hs)
    .custom<rtti::type_info>(rtti::type_info{"render_gpu", rtti::TYPE_CLASS_SYSTEM})
    .base<nb::system>()
    .func<&nb::render_gpu::window_width> ("window_width"_hs) .custom<rtti::func_info>(rtti::func_info{"window_width"})
    .func<&nb::render_gpu::window_height>("window_height"_hs).custom<rtti::func_info>(rtti::func_info{"window_height"})
    .func<&nb::render_gpu::display_scale>("display_scale"_hs).custom<rtti::func_info>(rtti::func_info{"display_scale"});
    entt::meta_factory<std::shared_ptr<nb::render_gpu>>{rtti::ctx_systems()}
    .type("render_gpu_shared"_hs)
    .ctor<&rtti::shared_ptr_builder<nb::render_gpu>>()
    .conv<std::shared_ptr<nb::system>>();
}
