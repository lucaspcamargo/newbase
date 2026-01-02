#pragma once

#include <newbase/mixins.hpp>
#include <SDL3/SDL.h>
#include <ryml.hpp>
#include <entt/entt.hpp>
#include <string>
#include <memory>

namespace nb {

enum step_phase {
    PREPARE,
    PRE_UPDATE,
    PHYSICS_UPDATE,
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

    virtual SDL_InitFlags sdl_subsystems(ryml::ConstNodeRef cfg) = 0;
    virtual entt::id_type metatype_id() = 0;

    virtual bool init(ryml::ConstNodeRef cfg) = 0;
    virtual bool step(step_phase) = 0;
    virtual bool event(SDL_Event*) = 0;

    // factory method
    static std::shared_ptr<system> build(const std::string &id, const void *cfgnode);

protected:
    // system callback map here
};

}
