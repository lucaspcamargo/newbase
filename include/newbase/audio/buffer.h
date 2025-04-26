#pragma once

#include <newbase/audio/types.h>
#include <vector>
#include <cassert>
#include <cstddef>
#include <utility>

namespace nb
{

// A copyable, movable, generic audio buffer
// Always stores interleaved data when not mono
// Channel ordering/assignment is same as SDL3
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
        if(data)
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

    // span type
    // a view of a subsection of a buffer
    // ideally we'd use ranges but this framework is C++17, not C++20
    // to be used for producer pull (and other processing purposes :)
    class span final
    {
    public:
        span() = delete;

        explicit span(const span &other):
            m_buf(other.m_buf),
            m_frame_start(other.m_frame_start),
            m_frame_len(other.m_frame_len)
        {
        }

        explicit span(audio_buffer &buf):
            m_buf(buf),
            m_frame_start(0),
            m_frame_len(buf.frames())
        {
        }

        explicit span(audio_buffer &buf, size_t start_frame, size_t end_frame):
            m_buf(buf),
            m_frame_start(start_frame),
            m_frame_len(end_frame-start_frame)
        {
            assert(end_frame >= start_frame);
        }

        span(span&&) = delete;

        ~span() = default;

        std::byte& operator[](int i) {
            return m_buf.data()[(i+m_frame_start)*m_buf.frame_stride()];
        }

        std::byte const& operator[](int i) const {
            return m_buf.data()[(i+m_frame_start)*m_buf.frame_stride()];
        }

        std::size_t frames() const {
            return m_frame_len;
        }

        std::size_t size() const {
            return (m_frame_len)*m_buf.frame_stride();
        }

        std::byte* begin() {
            return m_buf.data().data() + (m_frame_start*m_buf.frame_stride());
        }

        const std::byte* begin() const {
            return m_buf.data().data() + (m_frame_start*m_buf.frame_stride());
        }

        std::byte* end() {
            return begin() + size();
        }

        const std::byte* end() const {
            return std::as_const(*this).begin() + size();
        }

        bool empty() const 
        {
            return !m_frame_len;
        }

        span from(size_t frame_index)
        {
            return span(m_buf,
                std::max(m_frame_start+frame_index, m_frame_start),
                m_frame_start+m_frame_len);
        }
        
        span until(size_t frame_index)
        {
            return span(m_buf, 
                m_frame_start, 
                std::min(m_frame_start+m_frame_len, m_frame_start+frame_index));
        }

        audio_buffer & buffer_ref() {return m_buf;}
        const audio_buffer & buffer_ref() const {return m_buf;}
    private:
        audio_buffer &m_buf;
        size_t m_frame_start;
        size_t m_frame_len;
    };

    span as_span()
    {
        return span(*this);
    }

private:
    audio_spec m_spec {};
    size_t m_frames {0};
    std::vector<std::byte> m_data {};
};

}