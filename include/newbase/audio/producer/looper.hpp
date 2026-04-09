#pragma once

#include <newbase/audio/producer.hpp>
#include <memory>
#include <cassert>

namespace nb
{

// An audio producer that gets data from a stream
// It can loop the stream from a specified loop point, by rewinding and seeking the stream
class audio_producer_looper : public audio_producer
{
public:
    // loop_count: 0 = infinite, N = play exactly N times.
    audio_producer_looper(std::shared_ptr<audio_producer> prod, size_t loop_frame = 0, size_t loop_count = 0) :
        m_prod(prod),
        m_loop_frame(loop_frame),
        m_loop_count(loop_count),
        m_play_count(0),
        m_curr(0)
    {
        assert(m_prod.operator bool());
        // inner producer must be seekable, or at least resetable if loop frame is zero
        assert(m_prod->is_seekable() || (m_prod->is_resetable() && !loop_frame));
    }

    void set_loop_count(size_t loop_count)
    {
        m_loop_count = loop_count;
    }

    ~audio_producer_looper() override = default;

    bool is_seekable() override {return m_prod->is_seekable();} 
    bool is_complete() override
    {
        // Infinite loop (loop_count==0) never completes.
        // Finite loop completes once play_count reaches loop_count and inner is done.
        if (m_loop_count == 0) return false;
        return m_play_count >= m_loop_count && m_prod->frames_left() == 0;
    }
    bool is_resetable() override {return m_prod->is_resetable() || m_prod->is_seekable();}
    audio_spec spec() override {return m_prod->spec();}

    bool seek(size_t frame_index) override
    {
        if(!is_seekable())
            return false;

        if(m_prod->is_seekable())
        {
            if(m_prod->seek(frame_index))
            {
                m_curr = frame_index;
                return true;
            }   
        }
        else if(!frame_index)
        {
            return reset();
        }

        return false;
    }
    
    bool reset() override
    {
        if(!is_resetable())
            return false;

        if(m_prod->is_resetable() && m_prod->reset())
        {
            m_curr = 0;
            return true; 
        }
        else if(m_prod->is_seekable() && m_prod->seek(0))
        {
            m_curr = 0;
            return true;
        }

        return false;
    }

    size_t frames_left() override {return 0;} // because loop

    size_t frames_pull(audio_buffer::span dst, size_t max_frames) override
    {
        size_t produced = 0;
        while(produced < max_frames)
        {
            // if we are at the end
            if(m_prod->frames_left() == 0)
            {
                if(m_loop_frame)
                    seek(m_loop_frame);
                else
                    reset();
            }

            size_t goal = max_frames - produced;
            size_t produced_now = m_prod->frames_pull(dst.from(produced), goal);
            if(!produced_now)
                break; // something is fishy

            m_curr += produced_now;
            produced += produced_now;
        }
        return produced;
    }

private:
    std::shared_ptr<audio_producer> m_prod;
    size_t m_loop_frame;
    size_t m_loop_count; // 0 = infinite
    size_t m_play_count;
    size_t m_curr;
};

}