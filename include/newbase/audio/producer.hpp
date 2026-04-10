#pragma once

#include <newbase/audio/types.hpp>
#include <newbase/audio/buffer.hpp>
#include <memory>

namespace nb
{

// A generic interface for an audio data source
class audio_producer
{
public:
    audio_producer() = default;
    virtual ~audio_producer() = default;

    virtual bool is_seekable() {return false;} 
    virtual bool is_complete() {return false;} // whether the amount of frames that can be produced is known
    virtual bool is_resetable() {return false;}
    virtual audio_spec spec() = 0;  // producers must produce audio in a single format, and not care about conversions of any kind
    // it is the user's responsibility to provide buffers in the correct format for pulling

    virtual bool seek(size_t frame_index) {return false;}   // returns whether the seek was successful
    virtual bool reset() {return false;}    // returns whether reset was actually done
    virtual size_t frames_left() {return 0;} // if not complete, must return 0

    // Optional positional info — override in seekable producers.
    virtual size_t curr_frame()   const {return 0;}
    virtual size_t total_frames() const {return 0;}

    // produce up to max_frames samples into dst
    // returns the number of actually generated frames, in case the producer is done before
    virtual size_t frames_pull(audio_buffer::span dst, size_t max_frames) = 0;
};

}