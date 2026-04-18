#pragma once

#include <newbase/system.hpp>
#include <entt/entt.hpp>
#include <string>

namespace nb {

class textext : public system
{
public:
    textext()  = default;
    ~textext() = default;

    SDL_InitFlags sdl_subsystems(ryml::ConstNodeRef) override { return 0; }
    entt::id_type metatype_id() override { return entt::hashed_string{"textext"}.value(); }

    bool init(ryml::ConstNodeRef cfg) override;
    bool step(step_phase phase) override;
    bool event(SDL_Event*) override { return true; }

    void set_text(entt::entity ent, std::string text);
};

} // namespace nb
