#include <newbase/res/wav.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/log.hpp>

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_audio.h>

using namespace nb;

bool rwav::do_load()
{

    log::info("[rwav] loading: %x", id());
    valid = false;
    std::vector<char> data;
    if(!rman().read_all_sync(id(), data))
    {
        log::error("[rwav] data loading failed: %x", id());
        return false;
    }

    auto ios = SDL_IOFromConstMem(data.data(), data.size());
    if(!ios)
    {
        log::error("[rwav] io create failed: %x", id());
        return false;
    }

    SDL_AudioSpec sdl_spec;

    if(SDL_LoadWAV_IO(ios, true, &sdl_spec, &buf, &len))
    {
        spec.from_sdl(sdl_spec);
        valid = spec.format != audio_format::UNKNOWN;
    }
    else
    {
        log::error("[rwav] decode failed: %x: %s", id(), SDL_GetError());
        return false;
    }

    return true;
}

rwav::~rwav()
{
    if(buf)
    {
        SDL_free(buf);
    }
}
