#pragma once

#include <newbase/audio/types.h>
#include <newbase/audio/buffer.h>
#include <newbase/audio/converter.h>
#include <cassert>

namespace nb 
{

// a simple audio mixer
// TODO make into a producer?
class audio_mixer
{
public:
    explicit audio_mixer(audio_spec spec, size_t buf_sz):
        m_spec(spec),
        m_work(spec.with_format(audio_format::FLOAT), buf_sz, nullptr),
        m_to_float(m_spec, spec.with_format(audio_format::FLOAT)),
        m_from_float(spec.with_format(audio_format::FLOAT), m_spec)
    {
        
    }

    // destination buffer must match internal audio spec
    // destination buffer 
    bool mixdown(audio_buffer::span &dst, std::vector<const audio_buffer::span*>srcs)
    {
        if(!dst.size())
            return;

        assert(m_spec == dst.buffer_ref().spec());
        
        for(const audio_buffer::span *src : srcs)
        {
            assert(dst.buffer_ref().spec() == src->buffer_ref().spec());
            assert(dst.frames() == src->frames());
        }

        // clear gather buffer
        std::fill(dst.begin(), dst.end(), 0x00);
        float * const gather = reinterpret_cast<float*>(dst.begin());
        float * const work = reinterpret_cast<float*>(m_work.data().data());

        for(const audio_buffer::span* src : srcs)
        {
            m_to_float.put(*src);
            size_t produced = m_to_float.take(m_work.as_span());
            assert(produced == dst.frames());   // converter does not resample, this should be always true
            for(size_t curr = 0; curr < (produced*m_spec.channels); curr++)
            {
                gather[curr] += work[curr];
            }
        }
    }

private:
    audio_spec m_spec;
    audio_buffer m_work;
    audio_converter m_to_float;
    audio_converter m_from_float;
};

}