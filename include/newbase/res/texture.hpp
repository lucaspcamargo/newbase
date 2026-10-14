#pragma once

#include <newbase/render/texture.hpp>
#include <newbase/res/resource.hpp>
#include <SDL3/SDL_surface.h>
#include <entt/entt.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nb {

class rtexture : public resource
{
public:
    explicit rtexture(entt::id_type id = 0) : resource(id, entt::hashed_string{"rtexture"}.value()) {}
    ~rtexture() override;

    // takes a surface and owns it
    // transitions to loaded state immediately
    // for usage with ANONYMOUS RESOURCES ONLY
    void load_from(SDL_Surface* surf);

    struct mip_level
    {
        size_t offset {0};
        size_t size {0};
    };

    render::texture_type type {render::texture_type::IMAGE_2D};
    render::texture_format format {render::texture_format::INVALID};
    render::texture_sample_count sample_count {render::texture_sample_count::SAMPLES_1};
    render::texture_flags flags {
        render::texture_flags::SDL_SURFACE | render::texture_flags::NEEDS_UPLOAD
    };

    uint32_t width {0};
    uint32_t height {0};
    uint32_t depth {1};

    // Number of image layers, including cubemap faces (six per cube).
    uint32_t layer_count {1};

    // For uploaded textures, entries identify mip payloads in data. For a
    // target-only texture, the vector size can request allocated mip levels.
    std::vector<mip_level> mipmaps;
    std::vector<uint8_t> data;

    SDL_Surface *surf {nullptr};

    // opaque pointer for renderer usage, do not touch
    // unless you are the renderer, of course
    void *rptr {nullptr};

    // use nearest neighbor scaling when rendering
    // TODO replace by real enum when we get 3D?
    bool nearest {false};

    // whether to setup the gpu texture format
    // to convert the texture data to linear
    // space when sampling
    // ignored for 2D rendering
    bool srgb_conversion {false};

    // optional callback: reload the CPU surface from the resource manager (set by the loader).
    // Returns a freshly allocated SDL_Surface* that the caller owns, or nullptr on failure.
    SDL_Surface* (*reload_surface)(entt::id_type asset_id) {nullptr};

    // cleanup callback mechanism
    // to be used by the renderer
    void (*on_delete)(rtexture &t, void *uptr) {nullptr};
    void *on_delete_uptr {nullptr};

protected:
    bool do_load() override;
};

}
