#pragma once

#include <newbase/audio/types.hpp>
#include <memory>

namespace nb
{

class audio_stream_p;

// An audio stream that holds on to incoming samples
// and emits them upon request on the output end. 
// It converts between different audio_specs.
// A simple wrapper for SDL_AudioStream

class audio_stream final
{
public:
    audio_stream();
    ~audio_stream();

    bool pause();
    bool is_paused();
    bool resume();

    size_t frames_waiting();
    void frames_push(const audio_buffer &buf);
    void frames_pull(audio_buffer buf);
    size_t frames_clear();
    size_t frames_flush();

private:
    audio_stream_p *_d;
};

}