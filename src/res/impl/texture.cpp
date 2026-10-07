#include "newbase/res/resource.hpp"
#include <newbase/res/texture.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/log.hpp>

#include <entt/core/hashed_string.hpp>
#include <SDL3/SDL_pixels.h>
#include <stb_image.h>

#include <fstream>
#include <vector>
#include <cstdint>
#include <cstdio> // or <format> in C++20

void save_vector_to_file(uint32_t id, const uint8_t *data, size_t len) {
    // 1. Format the filename
    char filename[32];
    std::snprintf(filename, sizeof(filename), "0x%08x.bin", id);

    // 2. Open file in binary mode and write raw buffer
    std::ofstream file(filename, std::ios::binary);
    if (file.is_open()) {
        file.write(reinterpret_cast<const char*>(data), len);
    }
}

using namespace nb;
using entt::operator""_hs;


static SDL_Surface* load_texture_surface_from_data(entt::id_type id, const uint8_t *data, size_t len)
{
    int w, h, chs;
    auto ptr = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(data),
                                     static_cast<int>(len), &w, &h, &chs, 4);
    if (!ptr)
    {
        log::error("[rtexture] surface: stbi decode failed: %x", id);
        save_vector_to_file(id, data, len);
        return nullptr;
    }
    log::info("[rtexture] surface: loaded: %dx%d, %dchs", w, h, chs);
    SDL_Surface* tmp  = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, ptr, w * 4);
    SDL_Surface* surf = SDL_DuplicateSurface(tmp);
    SDL_DestroySurface(tmp);
    stbi_image_free(ptr);
    return surf;
}

// Standalone surface loader: reads asset bytes via the resource manager and decodes
// them with stb_image.  Returns a caller-owned SDL_Surface*, or nullptr on failure.
static SDL_Surface* load_texture_surface(entt::id_type id)
{
    std::vector<char> data;
    if (!rman().read_all_sync(id, data))
    {
        log::error("[rtexture] surface: data loading failed: %x", id);
        return nullptr;
    }
    SDL_Surface* surf = load_texture_surface_from_data(id, (const uint8_t*) data.data(), data.size());
    return surf;
}


rtexture::~rtexture()
{
    if(on_delete)
        on_delete(*this, on_delete_uptr);
    if(surf)
        SDL_DestroySurface(surf);
}


// texture loader
bool rtexture::do_load()
{
    log::info("[rtexture] loading: %x", id());

    // we could be a sub-resource
    // figure out if we have a parent
    auto hnd_it = rman().handles().find(id());
    if(hnd_it != rman().handles().end())
    {
        const auto &hnd = hnd_it->second;
        if(hnd.parent_id != 0)
        {
            // we are a subresource
            auto parent = rman().load_sync("rgltf"_hs, hnd.parent_id);
            if(parent)
            {
                std::vector<uint8_t> data {};
                bool got = parent->get_subresource_data(id(), data);
                if(got && data.size())
                {
                    surf = load_texture_surface_from_data(id(), data.data(), data.size());
                }
            }
        }
    }

    if(!surf)
    {
        // just load our surface directly, then
        surf = load_texture_surface(id());
    }

    rptr            = nullptr;
    uploaded       = false;
    reload_surface = load_texture_surface;
    if(surf)
    {
        width  = surf->w;
        height = surf->h;
    }
    return surf != nullptr;
}

void rtexture::load_from(SDL_Surface* s)
{
    assert(state() == resource_state::CREATED && s);
    surf = s;
    rptr            = nullptr;
    uploaded       = false;
    if(surf)
    {
        width  = surf->w;
        height = surf->h;
    }
    force_state(resource_state::LOADED);
}
