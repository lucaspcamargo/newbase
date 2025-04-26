#include <newbase/audio/types.h>
#include <SDL3/SDL_audio.h>

using namespace nb;

void audio_spec::to_sdl(SDL_AudioSpec &out)
{
    out.channels = channels;
    out.freq = frequency;
    out.format = SDL_AUDIO_UNKNOWN;

    switch(format)
    {
        case audio_format::FLOAT:
            out.format = SDL_AUDIO_F32;
            break;
        case audio_format::S16:
            out.format = SDL_AUDIO_S16;
            break;
        case audio_format::U8:
            out.format = SDL_AUDIO_U8;
            break;
        case audio_format::S8:
            out.format = SDL_AUDIO_S8;
            break;
    }
}


void audio_spec::from_sdl(const SDL_AudioSpec &in)
{
    channels = in.channels;
    frequency = in.freq;
    format = audio_format::UNKNOWN;

    switch(in.format)
    {
        case SDL_AUDIO_F32:
            format = audio_format::FLOAT;
            break;
        case SDL_AUDIO_S16:
            format = audio_format::S16;
            break;
        case SDL_AUDIO_U8:
            format = audio_format::U8;
            break;
        case SDL_AUDIO_S8:
            format = audio_format::S8;
            break;
    }
}