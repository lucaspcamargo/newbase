#pragma once

#include <atomic>
#include <cstddef>
#include <memory>

namespace nb {

// Feedback block shared between a looper/vorbis producer (audio thread)
// and the graphplan draw_fn (main thread).
//
// Audio thread writes state fields; main thread reads them.
// Main thread writes cmd_* fields; audio thread consumes them.
struct vorbis_feedback
{
    // State (audio thread → main thread)
    std::atomic<size_t> curr_frame;
    std::atomic<size_t> total_frames;
    std::atomic<size_t> play_count;
    std::atomic<size_t> loop_count;

    // Commands (main thread → audio thread)
    std::atomic<bool>   cmd_rewind;
    std::atomic<bool>   cmd_seek;
    std::atomic<size_t> cmd_seek_frame;

    vorbis_feedback()
        : curr_frame(0), total_frames(0), play_count(0), loop_count(0)
        , cmd_rewind(false), cmd_seek(false), cmd_seek_frame(0)
    {}
};

} // namespace nb
