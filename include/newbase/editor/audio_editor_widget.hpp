#pragma once

#include <newbase/audio/types.hpp>
#include <cstddef>
#include <vector>

namespace nb {

// Read-only waveform viewer widget.
// Call open() once with raw interleaved PCM data, then draw() each frame.
class audio_editor_widget
{
public:
    audio_editor_widget() = default;

    void open(const void* pcm_data, size_t byte_len, audio_spec spec);
    void draw();

private:
    std::vector<float> _waveform; // downsampled mono peak envelope
    audio_spec         _spec     {};
    size_t             _total_frames {0};
};

} // namespace nb
