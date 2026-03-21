#pragma once

#include <newbase/system.hpp>

namespace nb {
    
class editor final : public nb::system
{
public:
    editor() {}
    ~editor() {}


    SDL_InitFlags sdl_subsystems(ryml::ConstNodeRef cfg) override {return 0;}
    entt::id_type metatype_id() override { return entt::hashed_string{"editor"}.value(); }

    bool init(ryml::ConstNodeRef cfg) override;
    bool step(step_phase) override;
    bool event(SDL_Event*) override;

private:
    void _draw_main_menu();
};

}