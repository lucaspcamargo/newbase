#include <newbase/audio/audio.h>

using namespace nb;

audio::audio()
{

}

audio::~audio()
{

}

SDL_InitFlags audio::sdl_subsystems()
{
    return SDL_INIT_AUDIO;
}

bool audio::init(int argc, char ** argv)
{
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


