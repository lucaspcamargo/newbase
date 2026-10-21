#pragma once

#include "newbase/layer.hpp"
#include "newbase/render/pipeline.hpp"
#include "newbase/render/types.hpp"
#include <newbase/sys/render_gpu/targets.hpp>
#include <newbase/sys/render_gpu/pipelines.hpp>
#include <newbase/log.hpp>
#include <SDL3/SDL_gpu.h>
#include <limits>


// we keep this file header-only, as we want inlining into render_gpu

namespace nb::gpu
{

    /// The render pass controller.
    /// A stateful tracking mechanism for starting and ending render passes,
    /// according to desired render targets.
    /// Also helps build pipelines according to the buffer attachments of the
    /// current render pass.
    struct pass_control
    {

        pass_control(target_manager &tman) : m_targets(tman) {}

        /// Binds render targets for a layer, creating a new render pass
        /// if necessary. If there is a current render pass, it
        /// is finalized.
        /// @returns Whether the target was already bound, or bind succeeded.
        bool bind_target(const render_layer &l, SDL_GPUCommandBuffer *cmds)
        {
            auto tid = l.target_id;

            // already bound to the correct target
            if(tid == m_curr_target)
                return true;

            unbind();

            //log::critical("PASS CHANGE target %d", l.target_id);

            const auto *block = m_targets.target_get(tid);
            if(!block)
            {
                log::warn("[render_gpu] pass_control: cannot find target %d for layer with order %d", (int) tid, (int)l.order);
                return false;
            }

            if(!block->color_texture_count)
            {
                log::warn("[render_gpu] pass_control: no color texture: target %d, layer.order %d", (int) tid, (int)l.order);
                return false;
            }

            // TODO multiple color targets

            auto *tblock = (texture_block*) block->color_textures[0]->rptr;

            if(!tblock || !tblock->gtex)
            {
                log::warn("[render_gpu] pass_control: no GPU color texture: target %d, layer.order %d", (int) tid, (int)l.order);
                return false;
            }

            // TODO allow usage of don't care or clear. add flag in layer to preserve preserve target contents or not
            // this can be safely ignored by the 2d renderer
            // preserve + clear = use load op + render batched 2d clear geom
            // preserve only = use load op
            // clear only = use clear op
            // none = use ignore op
            // for now this works because we are drawing a 2d "clear quad" for all layers with clears

            SDL_GPUColorTargetInfo color_info {};
            color_info.texture = tblock->gtex;
            color_info.mip_level = block->pt_info.color_mip_level[0];
            color_info.layer_or_depth_plane = block->pt_info.color_layer_index[0];
            color_info.clear_color = (SDL_FColor){l.clear_r, l.clear_g, l.clear_b, 1.0f};
            color_info.load_op = SDL_GPU_LOADOP_LOAD;
            color_info.store_op = SDL_GPU_STOREOP_STORE;
            color_info.cycle = false;  // cannot cycle a LOADOP_LOAD


            SDL_GPUDepthStencilTargetInfo depth_info {};
            bool use_depth = block->desc.has_depth && block->depth_texture;
            if(use_depth)
            {
                if(block->pt_info.depth_mip_level > std::numeric_limits<Uint8>::max() ||
                   block->pt_info.depth_layer_index > std::numeric_limits<Uint8>::max())
                {
                    log::warn("[render_gpu] pass_control: depth subresource is out of range: target %d, layer.order %d",
                              (int) tid, (int)l.order);
                    return false;
                }

                auto *dblock = (texture_block*) block->depth_texture->rptr;

                if(!dblock || !dblock->gtex)
                {
                    log::warn("[render_gpu] pass_control: no GPU depth texture: target %d, layer.order %d", (int) tid, (int)l.order);
                    return false;
                }
                depth_info.texture = dblock->gtex;
                depth_info.mip_level = static_cast<Uint8>(block->pt_info.depth_mip_level);
                depth_info.layer = static_cast<Uint8>(block->pt_info.depth_layer_index);
                depth_info.load_op = SDL_GPU_LOADOP_CLEAR;
                depth_info.clear_depth = 1.0f;
                depth_info.store_op = block->depth_sampling? SDL_GPU_STOREOP_STORE : SDL_GPU_STOREOP_DONT_CARE;
                depth_info.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
                depth_info.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
                depth_info.cycle = true;

            }

            // begin render pass
            m_curr_pass = SDL_BeginGPURenderPass(cmds, &color_info, 1, use_depth? &depth_info : nullptr);

            if(!m_curr_pass)
            {
                log::warn("[render_gpu] pass_control: cannot create pass: target %d, layer.order %d", (int) tid, (int)l.order);
                return false;
            }

            // ok to take raw pointer here, pass only lasts during rendering
            m_curr_block = block;
            m_curr_target = tid;

            return true;
        }

        /// Binds the attachments described by a collected graphics pass.
        /// Managed targets are resolved through target_manager; explicit texture
        /// attachments are resolved directly through their renderer GPU handles.
        bool bind_pass(const render::render_pass &pass, const render_layer &l,
                       SDL_GPUCommandBuffer *cmds)
        {
            unbind();

            bool has_explicit_target = pass.target.depth_texture != nullptr;
            for(auto *texture : pass.target.color_textures)
                has_explicit_target = has_explicit_target || texture != nullptr;
            if(pass.target.uses_managed_target() && has_explicit_target)
            {
                log::warn("[render_gpu] pass_control: managed and explicit attachments cannot be mixed");
                return false;
            }

            const target_block *block = nullptr;
            if(pass.target.uses_managed_target())
            {
                block = m_targets.target_get(pass.target.managed_target);
                if(!block)
                {
                    log::warn("[render_gpu] pass_control: unknown managed target %u",
                              pass.target.managed_target);
                    return false;
                }
            }

            SDL_GPUColorTargetInfo color_infos[render::PASS_MAX_COLOR_TARGETS] {};
            int color_count = 0;
            bool found_color_gap = false;
            for(int i = 0; i < render::PASS_MAX_COLOR_TARGETS; ++i)
            {
                rtexture *texture = nullptr;
                if(block)
                {
                    if(i < block->color_texture_count)
                        texture = block->color_textures[i].get();
                }
                else
                    texture = pass.target.color_textures[i];

                if(!texture)
                {
                    found_color_gap = true;
                    continue;
                }
                if(found_color_gap)
                {
                    log::warn("[render_gpu] pass_control: color attachments must be contiguous");
                    return false;
                }

                auto *tblock = static_cast<texture_block*>(texture->rptr);
                if(!tblock || !tblock->gtex)
                {
                    log::warn("[render_gpu] pass_control: color attachment %d is not GPU-ready", i);
                    return false;
                }

                auto &info = color_infos[color_count++];
                info.texture = tblock->gtex;
                info.mip_level = pass.targets.color_mip_level[i];
                info.layer_or_depth_plane = pass.targets.color_layer_index[i];
                info.clear_color = {l.clear_r, l.clear_g, l.clear_b, 1.0f};
                info.load_op = pass.clear_color ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
                info.store_op = SDL_GPU_STOREOP_STORE;
                info.cycle = false;
            }

            if(!color_count && !pass.target.depth_texture &&
               (!block || !block->depth_texture))
            {
                log::warn("[render_gpu] pass_control: pass has no attachments");
                return false;
            }

            SDL_GPUDepthStencilTargetInfo depth_info {};
            rtexture *depth_texture = block ? block->depth_texture.get() : pass.target.depth_texture;
            if(depth_texture)
            {
                if(pass.targets.depth_mip_level > std::numeric_limits<Uint8>::max() ||
                   pass.targets.depth_layer_index > std::numeric_limits<Uint8>::max())
                {
                    log::warn("[render_gpu] pass_control: depth subresource is out of range");
                    return false;
                }

                auto *dblock = static_cast<texture_block*>(depth_texture->rptr);
                if(!dblock || !dblock->gtex)
                {
                    log::warn("[render_gpu] pass_control: depth attachment is not GPU-ready");
                    return false;
                }
                depth_info.texture = dblock->gtex;
                depth_info.mip_level = static_cast<Uint8>(pass.targets.depth_mip_level);
                depth_info.layer = static_cast<Uint8>(pass.targets.depth_layer_index);
                depth_info.load_op = SDL_GPU_LOADOP_CLEAR;
                depth_info.clear_depth = 1.0f;
                depth_info.store_op = block && !block->depth_sampling
                    ? SDL_GPU_STOREOP_DONT_CARE : SDL_GPU_STOREOP_STORE;
                depth_info.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
                depth_info.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
                depth_info.cycle = (depth_info.store_op == SDL_GPU_STOREOP_DONT_CARE);
            }

            m_curr_pass = SDL_BeginGPURenderPass(
                cmds, color_count ? color_infos : nullptr, color_count,
                depth_info.texture ? &depth_info : nullptr);
            if(!m_curr_pass)
            {
                log::warn("[render_gpu] pass_control: cannot begin graphics pass: %s", SDL_GetError());
                return false;
            }

            m_curr_block = block;
            m_curr_target = block ? pass.target.managed_target : render::TARGET_INVALID;
            return true;
        }

        /// Finishes the current render pass, ensuring the ccurrent
        /// command buffer can be submitted.
        void unbind()
        {
            if(!m_curr_pass)
                return;

            SDL_EndGPURenderPass(m_curr_pass);
            m_curr_pass = nullptr;
            m_curr_block = nullptr;
            m_curr_target = render::TARGET_INVALID;
        }

        /// Gets the target data block for the currently bound target
        /// @returns the block, or nullptr if no target is bound
        const target_block * current_target_block () const
        {
            return m_curr_block;
        }


        /// Gets the current render pass.
        /// If no target is currently bound, returns nullptr.
        SDL_GPURenderPass* current() const
        {
            return m_curr_pass;
        }

    private:
        target_manager &m_targets;

        SDL_GPURenderPass *m_curr_pass {nullptr};
        render::target_id_t m_curr_target {render::TARGET_INVALID};
        const target_block *m_curr_block {nullptr};
    };


}
