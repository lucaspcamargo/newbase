#pragma once

#include <vector>

namespace nb
{

enum class audio_format
{
    UNKNOWN,
    F32,
    F16,
    S16,
    U16,
    S8,
    U8
};


struct audio_spec
{
    audio_format format;
    int channels;
    int frequency;
};


// forwards

class audio_buffer;
class audio_stream; 

}