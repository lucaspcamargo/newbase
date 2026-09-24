#include "entt/meta/policy.hpp"
#include "newbase/services/renderer_service.hpp"
#include <newbase/render/rtti.hpp>

#include <newbase/render/camera.hpp>
#include <newbase/render/types.hpp>
#include <newbase/render/target.hpp>
#include <newbase/layer.hpp>
#include <newbase/reflection/data.hpp>

#include <entt/meta/factory.hpp>
#include <entt/meta/meta.hpp>


using entt::operator""_hs;


void nb::render::_rtti_init_render()
{
    // TODO fit mode enum

    entt::meta_factory<viewport_t>{}
    .type("render_viewport"_hs)
    .custom<rtti::type_info>(rtti::type_info{.identifier="render_viewport", .type_class=rtti::TYPE_CLASS_NONE})
    .ctor<>()
    .data<&viewport_t::x, entt::as_ref_t>("x"_hs)
    .custom<rtti::data_info>(rtti::data_info{"x"})
    .data<&viewport_t::y, entt::as_ref_t>("y"_hs)
    .custom<rtti::data_info>(rtti::data_info{"y"})
    .data<&viewport_t::w, entt::as_ref_t>("w"_hs)
    .custom<rtti::data_info>(rtti::data_info{"w"})
    .data<&viewport_t::h, entt::as_ref_t>("h"_hs)
    .custom<rtti::data_info>(rtti::data_info{"h"});

    entt::meta_factory<nb::render_layer>{}
    .type("render_layer"_hs)
    .custom<rtti::type_info>(rtti::type_info{.identifier="render_layer", .type_class=rtti::TYPE_CLASS_NONE})
    .ctor<>()
    .data<&nb::render_layer::scene_id>("scene_id"_hs)
    .custom<rtti::data_info>(rtti::data_info{"scene_id"})
    .data<&nb::render_layer::layer_mask>("layer_mask"_hs)
    .custom<rtti::data_info>(rtti::data_info{"layer_mask"})
    .data<&nb::render_layer::camera>("camera"_hs)
    .custom<rtti::data_info>(rtti::data_info{"camera"})
    .data<&nb::render_layer::order>("order"_hs)
    .custom<rtti::data_info>(rtti::data_info{"order"})
    .data<&nb::render_layer::viewport, entt::as_ref_t>("viewport"_hs)
    .custom<rtti::data_info>(rtti::data_info{"viewport"})
    .data<&nb::render_layer::follow_ui, entt::as_ref_t>("follow_ui"_hs)
    .custom<rtti::data_info>(rtti::data_info{"follow_ui"})
    .data<&nb::render_layer::ui_overlays, entt::as_ref_t>("ui_overlays"_hs)
    .custom<rtti::data_info>(rtti::data_info{"ui_overlays"})
    .data<&nb::render_layer::target_id, entt::as_ref_t>("target_id"_hs)
    .custom<rtti::data_info>(rtti::data_info{"target_id"})
    .data<&nb::render_layer::follow_target>("follow_target"_hs)
    .custom<rtti::data_info>(rtti::data_info{"follow_target"})
    .data<&nb::render_layer::clear>("clear"_hs)
    .custom<rtti::data_info>(rtti::data_info{"clear"})
    .data<&nb::render_layer::clear_r>("clear_r"_hs)
    .custom<rtti::data_info>(rtti::data_info{"clear_r"})
    .data<&nb::render_layer::clear_g>("clear_g"_hs)
    .custom<rtti::data_info>(rtti::data_info{"clear_g"})
    .data<&nb::render_layer::clear_b>("clear_b"_hs)
    .custom<rtti::data_info>(rtti::data_info{"clear_b"});

    entt::meta_factory<target_desc>{}
    .type("render_target_desc"_hs)
    .custom<rtti::type_info>(rtti::type_info{.identifier="render_target_desc", .type_class=rtti::TYPE_CLASS_NONE})
    .ctor<>()
    .data<&target_desc::abs_width, entt::as_ref_t>("abs_width"_hs)
    .custom<rtti::data_info>(rtti::data_info{"abs_width"})
    .data<&target_desc::abs_height, entt::as_ref_t>("abs_height"_hs)
    .custom<rtti::data_info>(rtti::data_info{"abs_height"})
    .data<&target_desc::size_mode, entt::as_ref_t>("size_mode"_hs)
    .custom<rtti::data_info>(rtti::data_info{"size_mode"})
    .data<&target_desc::size_scale, entt::as_ref_t>("size_scale"_hs)
    .custom<rtti::data_info>(rtti::data_info{"size_scale"})
    .data<&target_desc::size_source, entt::as_ref_t>("size_source"_hs)
    .custom<rtti::data_info>(rtti::data_info{"size_source"})
    .data<&target_desc::has_depth, entt::as_ref_t>("has_depth"_hs)
    .custom<rtti::data_info>(rtti::data_info{"has_depth"})
    .data<&target_desc::init_clear, entt::as_ref_t>("init_clear"_hs)
    .custom<rtti::data_info>(rtti::data_info{"init_clear"});

    entt::meta_factory<target_ref>{}
    .type("render_target_ref"_hs)
    .custom<rtti::type_info>(rtti::type_info{.identifier="render_target_ref", .type_class=rtti::TYPE_CLASS_NONE})
    .ctor<renderer_service&, const target_desc&>()
    .func<&target_ref::id>("id"_hs)
    .custom<rtti::func_info>(rtti::func_info{"id"})
    .func<&target_ref::is_valid>("is_valid"_hs)
    .custom<rtti::func_info>(rtti::func_info{"is_valid"})
    .func<&target_ref::color_texture>("color_texture"_hs)
    .custom<rtti::func_info>(rtti::func_info{"color_texture"})
    .func<&target_ref::depth_texture>("depth_texture"_hs)
    .custom<rtti::func_info>(rtti::func_info{"depth_texture"});

    entt::meta_factory<camera_2d>{}
    .type("render_camera_2d"_hs)
    .custom<rtti::type_info>(rtti::type_info{.identifier="render_camera_2d", .type_class=rtti::TYPE_CLASS_NONE})
    .ctor<>()
    .data<&camera_2d::scale, entt::as_ref_t>("scale"_hs)
    .custom<rtti::data_info>(rtti::data_info{"scale"})
    .data<&camera_2d::fit_mode, entt::as_ref_t>("fit_mode"_hs)
    .custom<rtti::data_info>(rtti::data_info{"fit_mode"})
    .data<&camera_2d::fit_world_dims, entt::as_ref_t>("fit_world_dims"_hs)
    .custom<rtti::data_info>(rtti::data_info{"fit_world_dims"})
    .data<&camera_2d::fit_anchor, entt::as_ref_t>("fit_anchor"_hs)
    .custom<rtti::data_info>(rtti::data_info{"fit_anchor"})
    .func<&camera_2d::calc_world_bounds>("calc_world_bounds"_hs)
    .custom<rtti::func_info>(rtti::func_info{"calc_world_bounds"});
}
