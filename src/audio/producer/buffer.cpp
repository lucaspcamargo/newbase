#include <newbase/audio/producer/buffer.h>
#include <algorithm>
#include <cassert>

using namespace nb;

bool audio_producer_buffer::seek(size_t frame_index)
{
    m_curr = m_loop? frame_index % m_buf.frames() : std::min(m_buf.frames(), frame_index);
    return true;
}

bool audio_producer_buffer::reset()
{
    m_curr = 0;
    return true;
}

size_t audio_producer_buffer::frames_left()
{
    if(m_loop)
        return 0;
    
    return m_buf.frames() - m_curr;
}

size_t audio_producer_buffer::frames_pull(audio_buffer::span dst, size_t max_frames)
{
    assert(dst.buffer_ref().spec() == m_buf.spec());
    
    size_t frame_stride = dst.buffer_ref().frame_stride();
    size_t out_frames = m_loop? max_frames : std::min(frames_left(), max_frames);
    size_t out_bytes = out_frames * frame_stride;

    if(out_frames)
    {
        if(m_loop)
        {
            m_curr = (m_curr + out_frames) % m_buf.frames();
            size_t to_copy = out_frames;
            auto dst_it = dst.begin();
            while(to_copy)
            {
                size_t copy_now = std::min(to_copy, m_buf.frames()-m_curr);
                auto src_start = m_buf.data().begin()+(m_curr*frame_stride);
                auto src_end = src_start + (copy_now * frame_stride);
                dst_it = std::copy(src_start, src_end, dst_it);    
                m_curr = (m_curr+copy_now)%m_buf.frames();
            }
        }
        else
        {
            m_curr += out_frames;
            auto src_start = m_buf.data().begin()+(m_curr*frame_stride);
            auto src_end = src_start + out_bytes;
            std::copy(src_start, src_end, dst.begin());
        }
    }

    return out_frames;
}