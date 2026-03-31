#include <newbase/sgdk/api/types.h>
#include <newbase/sgdk/api/sys.h>
#include <newbase/sgdk/api/vdp.h>
#include <newbase/sgdk/sgdk_p.hpp>

#include <setjmp.h>
#include <SDL3/SDL.h>

// ---------------------------------------------------------------------------
// sys
// ---------------------------------------------------------------------------

void nb_sgdk_set_main(void (*fn)(bool))
{
    // The sgdk system may not exist yet when this is called, so we store the
    // pointer globally; sgdk_p::init() picks it up from here.
    if (nb::tl_current)
        nb::tl_current->game_main = fn;
    else
        nb::g_pending_game_main = fn;
}

bool VDP_waitVSync(void)
{
    nb::sgdk_p* p = nb::tl_current;
    // Signal the host that we have finished a frame
    SDL_SignalSemaphore(p->sem_host);
    // Block until the host renders and gives us the next frame
    SDL_WaitSemaphore(p->sem_game);

    // If the host requested an exit while we were blocked, unwind now
    if (p->exit_requested)
        longjmp(p->exit_jmp, 1);
    
    return false; // no framelag ever :)
}

void nb_sgdk_request_exit(void)
{
    nb::sgdk_p* p = nb::tl_current;
    if (p) longjmp(p->exit_jmp, 1);
}

// ---------------------------------------------------------------------------
// VDP port writes — forward to the sgdk_vdp owned by this thread's instance
// ---------------------------------------------------------------------------

void vdp_write_ctrl(uint32_t value)
{
    nb::tl_current->vdp.write_ctrl(value);
}

uint32_t vdp_read_ctrl(void)
{
    return nb::tl_current->vdp.read_ctrl();
}

void vdp_write_data(uint16_t value)
{
    nb::tl_current->vdp.write_data(value);
}

uint16_t vdp_read_data(void)
{
    return nb::tl_current->vdp.read_data();
}

uint16_t vdp_read_hvcounter(void)
{
    return nb::tl_current->vdp.read_hvcounter();
}

// ---------------------------------------------------------------------------
// Z80 stubs — no Z80 emulation on host
// ---------------------------------------------------------------------------

void     z80_write_busreq(uint16_t /*value*/) {}
uint16_t z80_read_busreq(void) { return 0; }
void     z80_write_reset(uint16_t /*value*/)  {}
