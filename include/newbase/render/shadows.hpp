#pragma once

#include "newbase/render/texture.hpp"
#include <newbase/render/config.hpp>
#include <newbase/res/texture.hpp>
#include <memory>

// our renderer-agnostic facilities for shadow mapping

namespace nb::render
{
    class shadow_manager final
    {
        struct map_array
        {
            uint32_t max;
            uint32_t alloc;
            uint32_t cap;
            uint32_t dims;
            std::shared_ptr<rtexture> tex;

            void setup(uint32_t max_layers, uint32_t initial_capacity, uint32_t layer_dims)
            {
                max = max_layers;
                cap = std::min(initial_capacity, max_layers);
                dims = layer_dims;
            }

            void reset()
            {
                alloc = cap = 0;
                tex.reset();
            }

            void allocate_texture()
            {
                if(!cap)
                {
                    tex.reset();
                    return;
                }
                tex = std::make_shared<rtexture>();
                tex->width = tex->height = dims;
                tex->depth = 1;
                tex->flags = render::texture_flags::TARGET;
                tex->format = render::texture_format::DEPTH_16;
                tex->layer_count = cap;
            }

            int allocate_layer()
            {
                if(alloc == max)
                    return -1; // already full

                if(!tex)
                    allocate_texture();
                else if(alloc >= cap)
                {
                    cap = std::min(max, cap*2);
                    allocate_texture();
                }

                return alloc++;
            }
        };

    public:
        shadow_manager(const config3d &cfg) : m_cfg(cfg)
        {
            init();
        }

        ~shadow_manager()
        {
            reset();
        }

        bool init()
        {
            m_punct.setup(m_cfg.point_shadow_layers_max,
                          m_cfg.point_shadow_layers_init,
                          m_cfg.point_shadow_resolution);
            m_dir.setup(m_cfg.dir_shadow_layers_max,
                        m_cfg.dir_shadow_layers_init,
                        m_cfg.dir_shadow_resolution);
            return true;
        }

        void reset()
        {
            // release our internal resources
            m_punct.reset();
            m_dir.reset();
        }

        void new_frame()
        {
            m_punct.alloc = 0;
            m_dir.alloc = 0;
        }

        int allocate_shadow_map(bool directional)
        {
            map_array &arr = directional? m_dir : m_punct;

            return arr.allocate_layer(); // pool exhausted, no shadow map for you, honey
        }

        rtexture* texture(bool directional) const { return (directional? m_dir : m_punct).tex.get(); }
        uint32_t resolution(bool directional) const { return (directional? m_dir : m_punct).dims; }

    private:
        const config3d &m_cfg;
        map_array m_punct;
        map_array m_dir;
    };
}
