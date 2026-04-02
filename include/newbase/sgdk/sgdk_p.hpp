#pragma once

#include <newbase/sgdk/emu/e_vdp.hpp>
#include <newbase/services/renderer_service.hpp>
#include <SDL3/SDL.h>
#include <setjmp.h>

namespace nb {

struct sgdk_p {
    // Set by nb_sgdk_set_main() before the engine starts.
    void (*game_main)(bool) = nullptr;
    // --- threading ---
    SDL_Thread*    thread    = nullptr;
    SDL_Semaphore* sem_host  = nullptr;   // host blocks here; game posts on vsync
    SDL_Semaphore* sem_game  = nullptr;   // game blocks here; host posts each frame

    // --- exit ---
    jmp_buf exit_jmp;
    bool    exit_requested = false;
    bool    exited         = false;

    // --- emulation ---
    sgdk_vdp vdp;

    // --- display ---
    SDL_Surface*                    vdp_surface = nullptr;  // RGBA32 320x224 CPU buffer
    renderer_service::texture_handle vdp_tex    = nullptr;  // GPU texture for ImGui

    // --- init / teardown ---
    bool init();
    void shutdown();
};

// Thread-local pointer to the sgdk_p that owns the current game thread.
extern thread_local sgdk_p* tl_current;

// Holds the game_main pointer set via nb_sgdk_set_main() before the sgdk
// system instance exists. Consumed by sgdk_p::init().
extern void (*g_pending_game_main)(bool);

} // namespace nb
