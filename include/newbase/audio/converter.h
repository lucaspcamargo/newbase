#pragma once

#include <newbase/audio/types.h>
#include <newbase/audio/buffer.h>
#include <newbase/mixins.h>

namespace nb {

class audio_converter final : public nocopy
{
public:
    explicit audio_converter(audio_spec in_spec, audio_spec out_spec);
    ~audio_converter();

    void put(const audio_buffer::span &in);
    void flush();
    size_t available() const;
    size_t take(audio_buffer::span out);

private:
    void *m_context;
    audio_spec m_in_spec;
    audio_spec m_out_spec;
};

}