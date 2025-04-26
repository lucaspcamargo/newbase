#pragma once

#include <newbase/audio/producer.h>

namespace nb
{

struct audio_producer_vorbis_p;

// An audio producer that gets data from a stream
class audio_producer_vorbis : public audio_producer
{
public:
    audio_producer_vorbis(const std::byte *buf, size_t len);
    ~audio_producer_vorbis() override;

    bool is_valid() const;

    bool is_seekable() override; 
    bool is_complete() override;
    bool is_resetable() override;
    audio_spec spec() override;

    bool seek(size_t frame_index) override;
    bool reset() override;
    size_t frames_left() override;

    size_t frames_pull(audio_buffer::span dst, size_t max_frames) override;

private:
    audio_producer_vorbis_p *_d;
};

}