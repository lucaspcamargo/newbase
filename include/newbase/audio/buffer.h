#pragma once

#include <newbase/audio/types.h>
#include <vector>

namespace nb
{

// A copyable, movable, generic audio buffer
// Always stores interleaved data (for now at least)
class audio_buffer final
{
public:
    audio_buffer() = delete;
    audio_buffer(audio_spec spec):
        m_spec(spec)
    {   
    }
    explicit audio_buffer(audio_spec spec, size_t frames, const std::byte *data = nullptr):
        m_spec(spec),
        m_frames(frames),
        m_data{}
    {
        auto sz = frame_stride()*frames;
        m_data.resize(sz);
        std::copy(data, data+sz, m_data.begin());
    }
    explicit audio_buffer(const audio_buffer &other)
    {
        m_spec = other.m_spec;
        m_frames = other.m_frames;
        m_data = other.m_data;
    }
    explicit audio_buffer(audio_buffer &&other):
        m_spec(std::move(other.m_spec)),
        m_frames(std::move(other.m_frames)),
        m_data(std::move(other.m_data))
    {
        other.m_frames = 0;
    }
    ~audio_buffer() = default;

    inline audio_spec spec() const {return m_spec; }
    inline audio_format format() const { return m_spec.format; }
    inline int channels() const { return m_spec.channels; }
    inline unsigned int frequency() const { return m_spec.frequency; }
    inline size_t frames() const { return m_frames; }
    inline size_t frame_stride() const { return audio_format_size(m_spec.format)*m_spec.channels; }
    inline size_t bytes() const { return m_data.size(); }

    std::vector<std::byte>& data() { return m_data; }
    const std::vector<std::byte>& data() const { return m_data; }

private:
    audio_spec m_spec {};
    size_t m_frames {0};
    std::vector<std::byte> m_data {};
};

}