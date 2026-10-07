#pragma once

#include <newbase/render/types.hpp>
#include <newbase/log.hpp>
#include <SDL3/SDL_gpu.h>
#include <unordered_map>

// TODO ADD TO RENDERER
//      stop creating a sampler inside every rtexture
//      we can have an engine-standard sampler description for mat in nb::render
//      use that to drive the native data
//      for 2D textures, we can have a conversion function that uses the info
//      on the texture resource, living in utils2D


namespace nb::gpu
{

// Our sampler object cache for render_gpu
// Much simpler than the pipeline cache, thankfully
class sampler_cache
{
public:
    using key_t = uint32_t;

    key_t hash_sampler_config(const render::sampler_config &sc)
    {
        // direct fnv-1a
        constexpr uint32_t PRIME_32  = 0x01000193u;
        constexpr uint32_t OFFSET_32 = 0x811C9DC5u;
        const uint8_t* bytes = static_cast<const uint8_t*>((void*)&sc);
        uint32_t hash = OFFSET_32;
        for (size_t i = 0; i < sizeof(render::sampler_config); ++i) {
            hash ^= bytes[i];
            hash *= PRIME_32;
        }
        return hash;
    }

    SDL_GPUSampler* get(SDL_GPUDevice *gdev, const render::sampler_config &sc)
    {
        auto hash = hash_sampler_config(sc);
        auto it = m_cache.find(hash);
        if(it == m_cache.end())
        {
            SDL_GPUSamplerCreateInfo info {};

            // compatible enums
            info.min_filter = (SDL_GPUFilter) sc.min_filter;
            info.mag_filter = (SDL_GPUFilter) sc.mag_filter;
            info.mipmap_mode = (SDL_GPUSamplerMipmapMode) sc.mip_filter;
            info.address_mode_u = (SDL_GPUSamplerAddressMode) sc.addr_mode_u;
            info.address_mode_v = (SDL_GPUSamplerAddressMode) sc.addr_mode_v;
            info.address_mode_w = (SDL_GPUSamplerAddressMode) sc.addr_mode_w;

            info.enable_anisotropy = sc.max_aniso > 1.0f;
            info.max_anisotropy = sc.max_aniso;
            info.max_lod = sc.max_lod; // 0 disables mipmapping

            // fixed to sane defaults
            info.enable_compare = false;
            info.compare_op = SDL_GPUCompareOp::SDL_GPU_COMPAREOP_INVALID;
            info.mip_lod_bias = 0.0f;
            info.min_lod = 0.0f;
            info.props = 0;

            auto samp = SDL_CreateGPUSampler(gdev, &info);
            if(!samp)
            {
                log::warn("[render_gpu] sampler_cache: failed to create sampler: '%s'", SDL_GetError());
            }

            it = m_cache.emplace(hash, samp).first;
        }

        return it->second;
    }

    void teardown(SDL_GPUDevice *gdev)
    {
        for(auto &[key, samp]: m_cache)
            SDL_ReleaseGPUSampler(gdev, samp);
        m_cache.clear();
    }

private:
    std::unordered_map<key_t, SDL_GPUSampler*> m_cache;
};

}
