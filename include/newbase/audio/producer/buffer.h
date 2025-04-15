#pragma once

#include <newbase/audio/producer.h>

namespace nb
{

// An audio producer that gets data from a stream
class audio_producer_buffer : public audio_producer
{
public:
    audio_producer_buffer(const audio_buffer &buf, bool loop = false) :
        m_buf(buf),
        m_curr(0),
        m_loop(loop)
    {}

    audio_producer_buffer(audio_buffer &&buf, bool loop = false) :
        m_buf(std::move(buf)),
        m_curr(0),
        m_loop(loop)
    {}

    ~audio_producer_buffer() override = default;

    bool is_seekable() override {return true;} 
    bool is_complete() override {return !m_loop;}
    bool is_resetable() override {return true;}
    audio_spec spec() override {return m_buf.spec();}

    bool seek(size_t frame_index) override;
    bool reset() override;
    size_t frames_left() override;

    size_t frames_pull(audio_buffer &buf, size_t max_frames) override;

private:
    const audio_buffer &m_buf;
    size_t m_curr;
    bool m_loop;
};

}