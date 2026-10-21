#pragma once

#include "newbase/render/texture.hpp"
#include <newbase/render/config.hpp>
#include <newbase/res/texture.hpp>
#include <newbase/log.hpp>
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
                log::info("[shadow_manager] map_array: setup: %u max, %u initial, size %ux%u", max_layers, initial_capacity, layer_dims, layer_dims);
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
                tex->type = render::texture_type::IMAGE_2D_ARRAY;
                tex->flags = render::texture_flags::TARGET;
                tex->format = render::texture_format::DEPTH_16;
                tex->layer_count = cap;
            }

            int allocate_layers(int count)
            {
                // Check if the requested layers exceed the absolute maximum limit
                if (alloc + count > max)
                    return -1; // Not enough total space available

                // Ensure capacity is large enough to fit the new allocation request
                if (!tex || (alloc + count > cap))
                {
                    while(cap < (alloc + count))
                        cap = std::min(cap * 2, max);

                    allocate_texture();
                }

                int start_index = alloc;
                alloc += count; // Advance allocation pointer by the requested count
                return start_index;
            }

            int allocate_layer()
            {
                return allocate_layers(1);
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
            log::info("[shadow_manager] init");

            m_punct.setup(m_cfg.point_shadow_layers_max,
                          m_cfg.point_shadow_layers_init,
                          m_cfg.point_shadow_resolution);
            m_dir.setup(m_cfg.dir_shadow_layers_max,
                        m_cfg.dir_shadow_layers_init,
                        m_cfg.dir_shadow_resolution);
            auto tex = m_fallback = std::make_shared<rtexture>();
            tex->width = tex->height = 1;
            tex->depth = 1;
            tex->type = render::texture_type::IMAGE_2D_ARRAY;
            tex->flags = render::texture_flags::TARGET;
            tex->format = render::texture_format::DEPTH_16;
            tex->layer_count = 1;
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

        int allocate_shadow_maps(bool directional, int count)
        {
            if(count <= 0)
                return -1;

            map_array &arr = directional? m_dir : m_punct;

            return arr.allocate_layers(count);
        }

        rtexture* texture(bool directional) const
        {
            auto real = (directional? m_dir : m_punct).tex.get();
            return real? real : m_fallback.get();
        }

        uint32_t resolution(bool directional) const { return (directional? m_dir : m_punct).dims; }

    private:
        const config3d &m_cfg;
        map_array m_punct;
        map_array m_dir;
        std::shared_ptr<rtexture> m_fallback;
    };
}
