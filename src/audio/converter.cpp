#include <newbase/audio/converter.hpp>
#include <SDL3/SDL_audio.h>

using namespace nb;


inline static void as_sdl_spec(const audio_spec &in, SDL_AudioSpec &out)
{
    out.channels = in.channels;
    out.freq = in.frequency;
    out.format = SDL_AUDIO_UNKNOWN;

    switch(in.format)
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

audio_converter::audio_converter(audio_spec in_spec, audio_spec out_spec) :
    m_context {nullptr},
    m_in_spec {in_spec},
    m_out_spec {out_spec}
{
    SDL_AudioSpec sdl_in, sdl_out;
    as_sdl_spec(m_in_spec, sdl_in);
    as_sdl_spec(m_out_spec, sdl_out);
    assert(sdl_in.format != SDL_AUDIO_UNKNOWN);
    assert(sdl_out.format != SDL_AUDIO_UNKNOWN);
    m_context = SDL_CreateAudioStream(&sdl_in, &sdl_out);
    assert(m_context);
}

audio_converter::~audio_converter()
{
    if(m_context)
        SDL_DestroyAudioStream(reinterpret_cast<SDL_AudioStream*>(m_context));
}


void audio_converter::put(const audio_buffer::span &in)
{
    SDL_AudioStream *cvt = reinterpret_cast<SDL_AudioStream*>(m_context);
    assert(in.buffer_ref().spec() == m_in_spec);
    assert(cvt);
    SDL_PutAudioStreamData(cvt, in.begin(), in.size());
}

void audio_converter::flush()
{
    SDL_AudioStream *cvt = reinterpret_cast<SDL_AudioStream*>(m_context);
    SDL_FlushAudioStream(cvt);
}

size_t audio_converter::available() const
{
    SDL_AudioStream *cvt = reinterpret_cast<SDL_AudioStream*>(m_context);
    const size_t frame_stride = audio_format_size(m_out_spec.format)*m_out_spec.channels;
    return static_cast<size_t>(SDL_GetAudioStreamAvailable(cvt)/frame_stride);
}

size_t audio_converter::take(audio_buffer::span out)
{
    SDL_AudioStream *cvt = reinterpret_cast<SDL_AudioStream*>(m_context);
    assert(out.buffer_ref().spec() == m_out_spec);
    assert(cvt);
    auto ret = SDL_GetAudioStreamData(cvt, out.begin(), out.size());
    assert(ret%out.buffer_ref().frame_stride() == 0);
    return static_cast<size_t>(ret/out.buffer_ref().frame_stride());
}