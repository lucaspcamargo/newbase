#include <newbase/res/writers.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/res/texture.hpp>
#include <newbase/log.hpp>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_pixels.h>
#include <stb_image_write.h>
#include <vector>

namespace nb {

// stbi_write callback: appends written bytes to a std::vector<char>
static void stbi_write_to_vec(void* ctx, void* data, int size)
{
    auto* buf = static_cast<std::vector<char>*>(ctx);
    buf->insert(buf->end(), static_cast<char*>(data), static_cast<char*>(data) + size);
}

bool rwriter_texture(nb::resource* res)
{
    auto* rt = static_cast<rtexture*>(res);

    // Ensure we have a CPU surface to encode
    SDL_Surface* surf = rt->surf;
    if (!surf && rt->reload_surface)
        surf = rt->reload_surface(rt->id());
    if (!surf)
    {
        log::error("[rwriter_texture] no surface available for asset %x", rt->id());
        return false;
    }

    // Convert to RGBA32 for encoding if needed
    SDL_Surface* rgba = (surf->format == SDL_PIXELFORMAT_RGBA32)
                        ? surf
                        : SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGBA32);
    if (!rgba)
    {
        log::error("[rwriter_texture] surface conversion failed for asset %x", rt->id());
        if (surf != rt->surf) SDL_DestroySurface(surf);
        return false;
    }

    std::vector<char> png;
    int ok = stbi_write_png_to_func(stbi_write_to_vec, &png,
                                    rgba->w, rgba->h, 4,
                                    rgba->pixels, rgba->pitch);

    if (rgba != surf) SDL_DestroySurface(rgba);
    if (surf != rt->surf) SDL_DestroySurface(surf);

    if (!ok || png.empty())
    {
        log::error("[rwriter_texture] PNG encoding failed for asset %x", rt->id());
        return false;
    }

    return rman().write_all_sync(rt->id(), png.data(), png.size());
}

} // namespace nb
