#pragma once

#include <newbase/mixins.h>
#include <SDL3/SDL.h>
#include <string>
#include <memory>

namespace nb {

enum step_phase {
    PREPARE,
    PRE_UPDATE,
    PHYSISCS_UPDATE,
    GENERAL_UPDATE,
    POST_UPDATE,
    PRE_RENDER,
    RENDER,
    POST_RENDER,
    _STEP_PHASE_COUNT
};

class system : public nocopy {
public:
    system() = default;
    virtual ~system() {}

    virtual SDL_InitFlags sdl_subsystems() = 0;

    virtual bool init(int argc, char **argv) = 0;
    virtual bool step(step_phase) = 0;
    virtual bool event(SDL_Event*) = 0;

    // factory method
    static std::shared_ptr<system> build(const std::string &id, const void *cfgnode);

protected:
    // system callback map here
};

}
