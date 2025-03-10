#include <newbase/audio/audio.h>
#include <newbase/sdl/utils.h>

using namespace nb;

static SDL_AudioDeviceID _dev_out{ 0 };
static SDL_AudioSpec _spec_out;
static int _bufsize_out; 

audio::audio()
{
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[audio] constructing");

}

audio::~audio()
{
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[audio] destroying");
    if(_dev_out)
    {
        SDL_CloseAudioDevice(_dev_out);
    }
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
    
    _dev_out = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if(!_dev_out)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[audio] could not open default device!");
        return false;
    }
    else
    {
        SDL_GetAudioDeviceFormat(_dev_out, &_spec_out, &_bufsize_out);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[audio] opened device '%s' (%d channels, %d Hz, %d frames)", SDL_GetAudioDeviceName(_dev_out), _spec_out.channels, _spec_out.freq, _bufsize_out);
    }

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


