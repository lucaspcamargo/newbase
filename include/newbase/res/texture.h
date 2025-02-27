#pragma once

#include <SDL3/SDL_render.h>

namespace nb {

struct rtexture {
    bool uploaded;
    SDL_Surface *surf;
    SDL_Texture *tex;
};

}
