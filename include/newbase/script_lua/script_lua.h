#pragma once

#include <newbase/system.h>

namespace nb {

struct script_lua_p;

class script_lua final : public system {
public:
    script_lua();
    ~script_lua();
    

    SDL_InitFlags sdl_subsystems() override;

    bool init(int argc, char **argv) override;
    bool step(step_phase) override;
    bool event(SDL_Event*) override;

private:
    // our own allocator provided to lua
    static void *l_alloc (void *ud, void *ptr, size_t osize, size_t nsize);
    script_lua_p *_d {nullptr};
};

}
