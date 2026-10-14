#include "entt/core/fwd.hpp"
#include "newbase/render/pass.hpp"
#include "newbase/render/pipeline.hpp"
#include "newbase/render/texture.hpp"
#include <newbase/sys/render_gpu/targets.hpp>
#include <newbase/render/types.hpp>
#include <newbase/res/texture.hpp>
#include <newbase/engine.hpp>
#include <newbase/log.hpp>
#include <memory>


using namespace nb;
using namespace nb::gpu;


static render::pass_target_info target_desc_to_pass_info(const render::target_desc &td);


void target_manager::clear()
{
    // just let texture_manager handle texture destruction
    m_targets.clear();
}


void target_manager::teardown()
{
    clear();
}

void target_manager::init( int w, int h, bool depth, bool depth_sampling )
{
    // register swapchain target as TARGET_DEFAULT
    // texture is left
    auto &block = m_targets.emplace(render::TARGET_DEFAULT,  target_block{}).first->second;
    block.color_texture_count = 1;
    block.color_textures[0] = std::make_shared<rtexture>(0);
    block.req_w = w;
    block.req_h = h;
    if(depth)
    {
        block.depth_texture = std::make_shared<rtexture>(0);
        block.depth_sampling = depth_sampling;
        block.pt_info.depth_target = render::texture_format::DEPTH_16;
    }
    block.desc = render::target_desc
    {
        .size_mode = render::target_size_mode::ABSOLUTE,
        .abs_width = (unsigned) w,
        .abs_height = (unsigned) h,
        .has_depth = depth
    };
    block.pt_info.samples = render::texture_sample_count::SAMPLES_1;
    // native color format in pt_info is filled in update_default,
    // after we obtain the swapchain texture and format
}

void target_manager::update_sizes(render::viewport_t ui_vp)
{
    if(m_resize_order_dirty)
        _size_reorder();

    // calculate render target sizes
    // GPU texture sizes will be applied before rendering
    for(auto tid: m_resize_order)
    {
        if(tid == render::TARGET_DEFAULT)
        {
            // main swapchain, our graph "anchor"
            // ui_vp should already be up to date, so do noting
            continue;
        }

        auto it = m_targets.find(tid);
        if(it == m_targets.end())
        {
            log::error("[render_2d] _targets_resize: unknown target id: %u", tid);
            continue;
        }

        auto &target = it->second;
        switch(target.desc.size_mode)
        {
            case render::target_size_mode::ABSOLUTE:
                target.req_w = std::max(1u, target.desc.abs_width);
                target.req_h = std::max(1u, target.desc.abs_height);
                break;

            case render::target_size_mode::UI_RELATIVE:
                target.req_w = std::max(1u, static_cast<unsigned>(
                    std::ceil(ui_vp.w * target.desc.size_scale)));
                target.req_h = std::max(1u, static_cast<unsigned>(
                    std::ceil(ui_vp.h * target.desc.size_scale)));
                break;

            case render::target_size_mode::TARGET_RELATIVE:
            {
                const auto oid = target.desc.size_source;
                auto it_other = m_targets.find(oid);
                if(it == m_targets.end())
                {
                    log::error("[render_gpu] update_sizes: %u: unknown source target id: %u. setting to 1x1", tid, oid);
                    target.req_w = target.req_h = 1;
                }
                else
                {
                    auto &other = it_other->second;
                    target.req_w = std::max(1u, static_cast<unsigned>(
                        std::ceil(other.req_w * target.desc.size_scale)));
                    target.req_h = std::max(1u, static_cast<unsigned>(
                        std::ceil(other.req_h * target.desc.size_scale)));
                }
            }
            break;
        }
    }

    // with render targets ready, we can adjust layer viewports that follow targets
    auto &layers = engine::instance().render_layers();
    for(auto &layer : layers)
    {
        if(layer.follow_target)
        {
            auto it = m_targets.find(layer.target_id);
            if(it!=m_targets.end())
            {
                auto &tgt = it->second;
                layer.viewport = {0, 0, (int) tgt.req_w, (int) tgt.req_h};
            }
        }
        else if (layer.follow_ui)
        {
            layer.viewport = ui_vp;
        }
    }
}


void target_manager::update_default(SDL_GPUTexture *color, SDL_GPUTextureFormat gfmt, int w, int h)
{
    auto it = m_targets.find(render::TARGET_DEFAULT);
    if(it == m_targets.end())
    {
        // make sure we have a default target registered
        log::error("[render_gpu] targets: update_default: no default target! missing init()?");
        return;
    }

    target_block &blk = it->second;
    blk.req_w = w;
    blk.req_h = h;
    blk.pt_info.color_targets[0] = m_texman.get_native_format(gfmt);
    for(int i = 1; i < render::PASS_MAX_COLOR_TARGETS; ++i)
        blk.pt_info.color_targets[i] = render::texture_format::INVALID;

    if(color)
        m_texman.set_non_owning(blk.color_textures[0].get(), color, gfmt, w, h);
    // if we are called without the swapchain, don't touch the target texture'
}


void target_manager::commit_textures(SDL_GPUDevice *gdev)
{
    for(auto &[id, block]: m_targets)
    {
        if(id != render::TARGET_DEFAULT)
            for(int i = 0; i < block.color_texture_count; i++)
                m_texman.color_target_setup(gdev, block.color_textures[i].get(),
                                            block.pt_info.color_targets[i], block.req_w, block.req_h);

        if(block.depth_texture)
            m_texman.depth_target_setup(gdev, block.depth_texture.get(),
                                        block.pt_info.depth_target,
                                        block.req_w, block.req_h,
                                        block.depth_sampling);
    }
}


render::target_id_t target_manager::target_create(const render::target_desc& desc)
{
    log::critical("TARGET_CREATE");

    auto new_id = m_next_id++;

    auto block = target_block {
        .color_texture_count = 1, // TODO make configurable?
        .desc = desc,
        .pt_info = target_desc_to_pass_info(desc), // TODO test?
        .req_w = desc.abs_width,
        .req_h = desc.abs_height,
        .depth_sampling = true  // TODO add to desc?
    };

    if(desc.has_depth)
        block.depth_texture = std::make_shared<rtexture>((entt::id_type) new_id);

    for(int i = 0; i < block.color_texture_count; ++i)
        block.color_textures[i] = std::make_shared<rtexture>((entt::id_type) new_id);

    if(desc.size_mode != render::target_size_mode::ABSOLUTE)
        m_resize_order_dirty = true;

    m_targets.emplace(new_id, std::move(block));

    return new_id;
}


bool target_manager::target_destroy(render::target_id_t id)
{
    log::critical("TARGET_DESTROY %d", (int) id);

    if (id == render::TARGET_DEFAULT || id == render::TARGET_INVALID)
        return false;

    auto it = m_targets.find(id);
    if (it == m_targets.end())
        return false;

    // ok, target exists, let's get rid of it

    if(it->second.desc.size_mode != render::target_size_mode::ABSOLUTE)
    {
        m_resize_order_dirty = true;
    }

    m_targets.erase(it); // textures will be destroyed by texture_manager

    return true;
}


const target_block* target_manager::target_get(render::target_id_t id)
{
    auto it = m_targets.find(id);
    if (it != m_targets.end())
        return &(it->second);
    return nullptr;
}


void target_manager::_size_reorder()
{
    // map all targets to a sequential vertex index
    std::unordered_map<render::target_id_t, int> mapping;
    int next_mapping = 0;
    mapping[render::TARGET_DEFAULT] = next_mapping++;
    for(const auto &[tid, target]: m_targets)
    {
        if(!mapping.contains(tid))
            mapping[tid] = next_mapping++;
        if(target.desc.size_mode == render::target_size_mode::TARGET_RELATIVE)
            if(!mapping.contains(target.desc.size_source))
                mapping[target.desc.size_source] = next_mapping++;
    }

    // build a graph using our mapping
    entt::adjacency_matrix<entt::directed_tag> graph {mapping.size()};
    for(const auto &[tid, target]: m_targets)
    {
        switch (target.desc.size_mode) {
            case render::target_size_mode::ABSOLUTE:
                break; // no dependencies

            case render::target_size_mode::UI_RELATIVE:
                graph.insert(mapping[render::TARGET_DEFAULT], mapping[tid]);
                break;

            case render::target_size_mode::TARGET_RELATIVE:
                graph.insert(mapping[target.desc.size_source], mapping[tid]);
                break;
        }
    }

    // now order graph via topo sort
    auto ok = m_resize_sorter.build(graph);
    if(!ok)
        log::error("[render_gpu] targets: _size_reorder: cyclic dependencies in render target sizes");

    // finally, remap vertex indices from graph back into target ids
    std::unordered_map<unsigned long long, render::target_id_t> invmap;
    for(auto &&[tid, vtx]:mapping)
        invmap[vtx] = tid;
    m_resize_order.clear();
    for(auto vtx: m_resize_sorter.execution_order())
    {
        auto tid = invmap[vtx];
        m_resize_order.push_back(tid);
    }

    m_resize_order_dirty = false;
}


bool target_manager::target_info_get(render::target_id_t id, render::pass_target_info &dst_info)
{
    auto *block = target_get(id);
    if(!block)
        return false;

    dst_info = block->pt_info;
    return true;
}


bool target_manager::prepare_gpu_pipeline_target_info(SDL_GPUGraphicsPipelineTargetInfo &pti,
                                      SDL_GPUColorTargetDescription *color_targets,
                                      render::pipeline_target_info native)
{
    int num_color_targets = 0;
    if(native.color_targets[0] == render::texture_format::INVALID)
        return false;
    for(int i = 0; i < render::PASS_MAX_COLOR_TARGETS; ++i)
        if(native.color_targets[i] != render::texture_format::INVALID)
        {
            color_targets[i].format = m_texman.get_gpu_format(native.color_targets[i]); // all targt
            color_targets[i].blend_state = {};
            color_targets[i].blend_state.enable_blend = false;
            color_targets[i].blend_state.enable_color_write_mask = false;
            num_color_targets++;
        }
    pti.num_color_targets = num_color_targets;

    if(native.depth_target != render::texture_format::INVALID)
    {
        pti.depth_stencil_format = m_texman.get_gpu_format(native.depth_target);
        pti.has_depth_stencil_target = true;
    }
    else
        pti.has_depth_stencil_target = false;

    return pti.has_depth_stencil_target || num_color_targets > 0;
}


// helpers

static render::pass_target_info target_desc_to_pass_info(const render::target_desc &td)
{
    render::pass_target_info ret;
    ret.color_targets[0] = render::texture_format::COLOR_RGBA_8888;
    // default to base mip / first layer
    for(int i = 0; i < render::PASS_MAX_COLOR_TARGETS; ++i) {
        ret.color_mip_level[i] = 0;
        ret.color_layer_index[i] = 0;
    }
    ret.depth_mip_level = 0;
    ret.depth_layer_index = 0;

    if(td.has_depth)
        ret.depth_target = render::texture_format::DEPTH_16;
    return ret;
}
