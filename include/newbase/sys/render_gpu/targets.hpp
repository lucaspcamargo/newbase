#pragma once

#include "newbase/render/types.hpp"
#include <newbase/sys/render_gpu/textures.hpp>
#include <newbase/res/texture.hpp>
#include <newbase/render/target.hpp>
#include <newbase/utility/topological_sort.hpp>
#include <array>
#include <memory>


namespace nb::gpu
{

    struct target_block
    {
        int color_texture_count {0};
        std::array<std::shared_ptr<rtexture>, 4> color_textures {};
        std::shared_ptr<rtexture> depth_texture {nullptr};
        render::target_desc desc;
        unsigned int req_w {0};
        unsigned int req_h {0};
        bool depth_sampling {false};
    };

    /// Our render target manager.
    /// TODO The target and sizing mechanism is the same as in render_2d,
    /// perhaps it can be refactored out and unified.
    struct target_manager
    {
        target_manager(texture_manager &tm) : m_texman(tm) {}
        ~target_manager() = default;

        /// release and unregister all render target resources
        void clear();

        /// on system destruction
        void teardown();

        /// intial setup of swapchain render target
        void init( int w, int h,  bool depth, bool depth_sampling );

        /// validate and update target dimensions
        /// also update dependant viewport sizes in current render layers
        void update_sizes(render::viewport_t ui_vp);

        /// update default target with the current swapchain texture
        void update_default(SDL_GPUTexture *color, SDL_GPUTextureFormat gfmt, int w, int h);

        /// update render target textures, applying current size information
        void commit_textures(SDL_GPUDevice *gdev);

        /// creates a new render target with the given description
        render::target_id_t target_create(const render::target_desc& desc);

        /// destroys a render target
        bool target_destroy(render::target_id_t id);

        /// gets a RO pointer for the target's data block,
        /// or nullptr if target is not found
        const target_block* target_get(render::target_id_t id);

    private:
        void _size_reorder();

        texture_manager &m_texman;
        std::unordered_map<render::target_id_t, target_block> m_targets;

        render::target_id_t m_next_id {1};
        util::topological_sorter m_resize_sorter;
        std::vector<render::target_id_t> m_resize_order;
        bool rt_resize_order_dirty {false};  // new rts were added or removed, reorder
        bool m_resize_order_dirty {false};
    };
}
