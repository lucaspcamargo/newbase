#pragma once

#include <vector>
#include <cstdint>
#include <cstddef>

namespace nb
{

enum class audio_format
{
    UNKNOWN,
    FLOAT,
    S16,
    U16,
    S8,
    U8
};


struct audio_spec
{
    audio_format format {audio_format::UNKNOWN};
    int channels {0};
    unsigned int frequency {0};

    bool operator ==(const audio_spec &other) const
    {
        return format == other.format
            && channels == other.channels
            && frequency == other.frequency;
    }

    audio_spec with_format(audio_format fmt)
    {
        return audio_spec {fmt, channels, frequency};
    }
};


// forwards
class audio_buffer;
class audio_stream; 
class audio_producer;

// utility inlines

inline size_t audio_format_size(audio_format fmt)
{
    switch(fmt)
    {
        case audio_format::FLOAT:
            return sizeof(float);
        case audio_format::S16:
            [[fallthrough]];
        case audio_format::U16:
            return sizeof(uint16_t);
        case audio_format::U8:
            [[fallthrough]];
        case audio_format::S8:
            return sizeof(uint8_t);
        default:
            return 0;
    }
}

}