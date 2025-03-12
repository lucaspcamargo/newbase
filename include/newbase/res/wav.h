#pragma once

#include <SDL3/SDL_audio.h>

namespace nb {

struct rwav {
    bool valid {false};
    SDL_AudioSpec spec{};
    uint8_t *buf;
    uint32_t len;
};

}