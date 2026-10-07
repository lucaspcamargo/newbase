#pragma once

#include "newbase/layer.hpp"
#include "newbase/render/types.hpp"
#include <newbase/sys/render_gpu/targets.hpp>
#include <newbase/sys/render_gpu/pipelines.hpp>
#include <newbase/log.hpp>
#include <SDL3/SDL_gpu.h>


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
            color_info.clear_color = (SDL_FColor){l.clear_r, l.clear_g, l.clear_b, 1.0f};
            color_info.load_op = SDL_GPU_LOADOP_LOAD;
            color_info.store_op = SDL_GPU_STOREOP_STORE;
            color_info.cycle = false;  // cannot cycle a LOADOP_LOAD


            SDL_GPUDepthStencilTargetInfo depth_info {};
            bool use_depth = block->desc.has_depth && block->depth_texture;
            if(use_depth)
            {
                auto *dblock = (texture_block*) block->depth_texture->rptr;

                if(!dblock || !dblock->gtex)
                {
                    log::warn("[render_gpu] pass_control: no GPU depth texture: target %d, layer.order %d", (int) tid, (int)l.order);
                    return false;
                }
                depth_info.texture = dblock->gtex;
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

        /// Fills the target blocks of a pipeline descriptor
        /// in accordance to the currently-bound render pass.
        /// If no render pass is currently bound, returns false.
        bool write_target_block(pipeline_desc &pipeline)
        {
            if(!m_curr_block)
                return false;

            for(int i = 0; i < m_curr_block->color_texture_count; i++)
                pipeline.color_formats[i] = ((texture_block*) m_curr_block->color_textures[i]->rptr)->gfmt;

            if(m_curr_block->desc.has_depth && m_curr_block->depth_texture)
                pipeline.depth_format = ((texture_block*) m_curr_block->depth_texture->rptr)->gfmt;

            pipeline.sample_count = SDL_GPU_SAMPLECOUNT_1;  // TODO MSAA :)

            return true;
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
