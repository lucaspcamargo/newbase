#include <newbase/res/texture.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/log.hpp>

#include <stb_image.h>

using namespace nb;


// Standalone surface loader: reads asset bytes via the resource manager and decodes
// them with stb_image.  Returns a caller-owned SDL_Surface*, or nullptr on failure.
static SDL_Surface* load_texture_surface(entt::id_type id)
{
    std::vector<char> data;
    if (!rman().read_all_sync(id, data))
    {
        log::error("[load_texture_surface] data loading failed: %x", id);
        return nullptr;
    }
    int w, h, chs;
    auto ptr = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(data.data()),
                                     static_cast<int>(data.size()), &w, &h, &chs, 4);
    if (!ptr)
    {
        log::error("[load_texture_surface] stbi decode failed: %x", id);
        return nullptr;
    }
    log::info("[load_texture_surface] loaded: %dx%d, %dchs", w, h, chs);
    SDL_Surface* tmp  = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, ptr, w * 4);
    SDL_Surface* surf = SDL_DuplicateSurface(tmp);
    SDL_DestroySurface(tmp);
    stbi_image_free(ptr);
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
    surf           = load_texture_surface(id());
    rptr            = nullptr;
    uploaded       = false;
    width = surf->w;
    height = surf->h;
    reload_surface = load_texture_surface;
    if(surf)
    {
        width  = surf->w;
        height = surf->h;
    }
    return surf != nullptr;
}
