#pragma once

#include <newbase/render/types.hpp>
#include <newbase/log.hpp>
#include <SDL3/SDL_gpu.h>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace nb::gpu
{

/// Owns GPU samplers, reusing one for each distinct sampler configuration.
class sampler_cache
{
public:
    using key_t = uint64_t;

    key_t hash_sampler_config(const render::sampler_config &sc) const
    {
        constexpr key_t PRIME = 1099511628211ull;
        key_t hash = 14695981039346656037ull;
        auto add = [&](uint32_t value) {
            for(int i = 0; i < 4; ++i)
            {
                hash ^= static_cast<uint8_t>(value >> (i * 8));
                hash *= PRIME;
            }
        };
        add(static_cast<uint32_t>(sc.min_filter));
        add(static_cast<uint32_t>(sc.mag_filter));
        add(static_cast<uint32_t>(sc.mip_filter));
        add(static_cast<uint32_t>(sc.addr_mode_u));
        add(static_cast<uint32_t>(sc.addr_mode_v));
        add(static_cast<uint32_t>(sc.addr_mode_w));
        uint32_t float_bits = 0;
        static_assert(sizeof(float_bits) == sizeof(sc.max_aniso));
        std::memcpy(&float_bits, &sc.max_aniso, sizeof(float_bits));
        add(float_bits);
        std::memcpy(&float_bits, &sc.max_lod, sizeof(float_bits));
        add(float_bits);
        return hash;
    }

    SDL_GPUSampler* get(SDL_GPUDevice *gdev, const render::sampler_config &sc)
    {
        auto hash = hash_sampler_config(sc);
        auto &entries = m_cache[hash];
        for(const auto &entry : entries)
            if(entry.config == sc)
                return entry.sampler;

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
        info.max_lod = sc.max_lod;

        // fixed to sane defaults
        info.enable_compare = false;
        info.compare_op = SDL_GPUCompareOp::SDL_GPU_COMPAREOP_INVALID;
        info.mip_lod_bias = 0.0f;
        info.min_lod = 0.0f;
        info.props = 0;

        auto samp = SDL_CreateGPUSampler(gdev, &info);
        if(!samp)
        {
            log::error("[render_gpu] sampler_cache: failed to create sampler: '%s'", SDL_GetError());
            return nullptr;
        }
        entries.push_back(entry {sc, samp});
        return samp;
    }

    void teardown(SDL_GPUDevice *gdev)
    {
        for(auto &[key, entries] : m_cache)
            for(auto &entry : entries)
                SDL_ReleaseGPUSampler(gdev, entry.sampler);
        m_cache.clear();
    }

private:
    struct entry
    {
        render::sampler_config config;
        SDL_GPUSampler *sampler;
    };
    std::unordered_map<key_t, std::vector<entry>> m_cache;
};

}
