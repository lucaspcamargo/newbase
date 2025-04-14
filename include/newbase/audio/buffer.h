#pragma once

#include <newbase/audio/types.h>
#include <vector>

namespace nb
{

// A copyable, movable, generic audio buffer
// What the streamers pull from resources, and push into streams
class audio_buffer final
{
public:
    audio_buffer();
    explicit audio_buffer(audio_format, size_t frames, int channels, const std::byte *data);
    explicit audio_buffer(const audio_buffer &other);
    explicit audio_buffer(audio_buffer &&other);
    ~audio_buffer();

    inline audio_format format() const { return m_fmt; }
    inline size_t frames() const { return m_frames; }
    inline int channels() const { return m_channels; }
    inline size_t frame_stride() const { return m_frame_stride; }
    inline size_t bytes() const { return m_data.size(); }

private:
    audio_format m_fmt {audio_format::UNKNOWN};
    size_t m_frames {0};
    int m_channels {0};
    size_t m_frame_stride {0};
    std::vector<std::byte> m_data {};
};

}