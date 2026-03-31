#pragma once

#include <newbase/res/resource.hpp>
#include <SDL3/SDL_render.h>
#include <entt/entt.hpp>

namespace nb {

struct rtexture : public resource {
    explicit rtexture(entt::id_type id = 0) : resource(id, entt::hashed_string{"rtexture"}.value()) {}

    bool uploaded {false};
    SDL_Surface *surf {nullptr};
    SDL_Texture *tex {nullptr};

    // Optional: reload the CPU surface from the resource manager (set by the loader).
    // Returns a freshly allocated SDL_Surface* that the caller owns, or nullptr on failure.
    SDL_Surface* (*reload_surface)(entt::id_type asset_id) {nullptr};
};

}
