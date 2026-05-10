#pragma once

#include <newbase/audio/producer.hpp>
#include <cstdint>
#include <cstddef>

namespace nb {

struct audio_producer_lpc_p;

// Audio producer implementing TMS5220-compatible LPC speech synthesis.
// Accepts vocabulary data in the TI serial ROM bitstream format
// (as used by the Speak & Spell, TI-99/4A Speech System, and compatible devices).
//
// Output spec: S16, mono, 8000 Hz.
// Supports reset. Seek is not supported (bitstream is variable-width per frame).
class audio_producer_lpc : public audio_producer
{
public:
    audio_producer_lpc(const uint8_t* data, size_t length);
    ~audio_producer_lpc() override;

    audio_spec spec() override;

    bool is_complete()  override { return true;  }
    bool is_resetable() override { return true;  }
    bool is_seekable()  override { return false; }

    bool   reset()       override;
    size_t frames_left() override;
    size_t curr_frame()  const override;
    size_t total_frames() const override;

    size_t frames_pull(audio_buffer::span dst, size_t max_frames) override;

private:
    audio_producer_lpc_p* _d;
};

} // namespace nb
