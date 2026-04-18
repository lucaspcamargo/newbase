#pragma once

#include <newbase/services/renderer_service.hpp>
#include <SDL3/SDL.h>
#include <cstddef>
#include <cstring>
#include <memory>

namespace nb {

// Shared state for the Visualizer node.
//
// Audio thread: calls push_samples() inside the tap_node's process().
// Main thread:  calls swap_and_get() in draw_fn, then draws + uploads texture.
struct visualizer_feedback
{
    static constexpr size_t BUF_SIZE = 2048; // samples stored per channel-0 pass

    // ---- audio thread writes (under mtx) ----
    float    write_buf[BUF_SIZE] {};
    size_t   write_pos {0};

    // ---- main thread reads (under mtx after swap) ----
    float    read_buf[BUF_SIZE] {};
    size_t   read_count {0};

    SDL_Mutex* mtx {nullptr};

    // ---- written by audio thread, read by main thread as a display hint ----
    // Acceptable racy read: worst case is a one-frame stale sample rate label.
    unsigned int sample_rate {44100};

    // ---- main thread only (no lock needed) ----
    bool                          spectrum_mode {false};
    SDL_Surface*                  surface {nullptr};
    renderer_service::texture_handle texture {nullptr};
    int                           tex_w {256};
    int                           tex_h {80};

    visualizer_feedback()
    {
        mtx = SDL_CreateMutex();
    }

    ~visualizer_feedback()
    {
        if (surface) { SDL_DestroySurface(surface); surface = nullptr; }
        // texture must be destroyed by the owner before the feedback is freed
        // (renderer_service is not accessible here)
        if (mtx) { SDL_DestroyMutex(mtx); mtx = nullptr; }
    }

    // Called from audio thread: push channel-0 samples into write_buf (ring).
    void push_samples(const float* interleaved, int channels, size_t frames)
    {
        if (!mtx || !interleaved || channels < 1) return;
        SDL_LockMutex(mtx);
        for (size_t i = 0; i < frames; ++i)
        {
            write_buf[write_pos % BUF_SIZE] = interleaved[i * static_cast<size_t>(channels)];
            ++write_pos;
        }
        SDL_UnlockMutex(mtx);
    }

    // Called from main thread: atomically copy write_buf → read_buf.
    // Returns number of valid samples in read_buf (clamped to BUF_SIZE).
    size_t snapshot()
    {
        if (!mtx) return 0;
        SDL_LockMutex(mtx);
        size_t n = write_pos < BUF_SIZE ? write_pos : BUF_SIZE;
        std::memcpy(read_buf, write_buf, n * sizeof(float));
        read_count = n;
        SDL_UnlockMutex(mtx);
        return read_count;
    }
};

} // namespace nb
