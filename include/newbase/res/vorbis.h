#pragma once

#include <SDL3/SDL_audio.h>
#include <vector>

namespace nb {

struct rvorbis {
    bool valid {false};
    bool decoded {false};
    std::vector<char> data {};
    SDL_AudioSpec spec {SDL_AUDIO_UNKNOWN, 0, 0};
    std::vector<char> frames {};
};

}