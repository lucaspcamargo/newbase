#pragma once

#include <newbase/res/resource.hpp>
#include <SDL3/SDL_render.h>

namespace nb {

struct rtexture : public resource {
    explicit rtexture(entt::id_type id = 0) : resource(id, entt::hashed_string{"rtexture"}.value()) {}

    bool uploaded {false};
    SDL_Surface *surf {nullptr};
    SDL_Texture *tex {nullptr};
};

}
