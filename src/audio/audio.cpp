#include <newbase/audio/audio.h>
#include <newbase/sdl/utils.h>

using namespace nb;

audio::audio()
{
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[audio] constructing");

}

audio::~audio()
{
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[audio] destroyed");
}

SDL_InitFlags audio::sdl_subsystems()
{
    return SDL_INIT_AUDIO;
}

bool audio::init(int argc, char ** argv)
{
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[audio] init");

    const auto drivers = get_all_strings(SDL_GetNumAudioDrivers, SDL_GetAudioDriver);
    const auto drivers_str = join_strings(drivers, ' ');
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[audio] available drivers: %s", drivers_str.c_str());

    return true;
}


bool audio::step(step_phase phase)
{
    return true;
}


bool audio::event(SDL_Event*)
{
    return true;
}


