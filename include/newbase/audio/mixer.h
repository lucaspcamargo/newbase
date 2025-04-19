#pragma once

#include <newbase/audio/types.h>
#include <newbase/audio/buffer.h>
#include <cassert>

namespace nb 
{

// a simple audio mixer
class audio_mixer
{
public:
    explicit audio_mixer(audio_spec spec):
        m_spec(spec)
    {
        
    }

    bool mixdown(audio_buffer &dst, std::vector<const audio_buffer*>srcs)
    {
        if(!dst.frames())
            return;
        assert(dst.format() != audio_format::UNKNOWN);
        assert(dst.channels());
        assert(dst.frequency());
        
        for(const audio_buffer* src : srcs)
        {
            assert(dst.spec() == src->spec());
            assert(dst.frames() == src->frames());
        }
        
        audio_buffer gather(dst.spec().with_format(audio_format::FLOAT), dst.frames(), nullptr);

        for(const audio_buffer* src : srcs)
        {
            
        }

    }

private:
    audio_spec m_spec;
};

}