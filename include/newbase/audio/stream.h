#pragma once

#include <newbase/audio/types.h>
#include <memory>

namespace nb
{

class audio_stream_p;

// A copyable, movable, generic audio buffer
// What the streamers pull from resources, and push into streams
class audio_stream final
{
public:
    audio_stream();
    ~audio_stream();

    // obtains a 
    std::shared_ptr<audio_stream> create_sub_stream();

    bool pause();
    bool is_paused();
    bool resume();

    size_t frames_waiting();
    void frames_push(const audio_buffer &buf);
    size_t frames_clear();
    size_t frames_flush(); 

private:
    audio_stream_p *_d;
};

}