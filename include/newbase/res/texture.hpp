#pragma once

#include <newbase/res/resource.hpp>
#include <SDL3/SDL_surface.h>
#include <entt/entt.hpp>

namespace nb {

class rtexture : public resource
{
public:
    explicit rtexture(entt::id_type id = 0) : resource(id, entt::hashed_string{"rtexture"}.value()) {}
    ~rtexture() override {
        if(on_delete)
            on_delete(*this, on_delete_uptr);
        if(surf)
            SDL_DestroySurface(surf);
    }

    bool uploaded {false};
    SDL_Surface *surf {nullptr};

    int width {0};
    int height {0};

    // opaque pointer for renderer usage
    void *rptr {nullptr};

    // optional callback: reload the CPU surface from the resource manager (set by the loader).
    // Returns a freshly allocated SDL_Surface* that the caller owns, or nullptr on failure.
    SDL_Surface* (*reload_surface)(entt::id_type asset_id) {nullptr};

    // cleanup callback mechanism
    // to be used by the renderer
    void (*on_delete)(rtexture &t, void *uptr) {nullptr};
    void *on_delete_uptr {nullptr};
};

}
