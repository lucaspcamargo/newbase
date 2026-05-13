#pragma once

#include <newbase/system.hpp>

namespace nb {

struct editor_p;

class editor final : public nb::system
{
public:
    editor();
    ~editor();

    SDL_InitFlags sdl_subsystems(ryml::ConstNodeRef cfg) override { return 0; }
    entt::id_type metatype_id() override { return entt::hashed_string{"editor"}.value(); }

    bool init(ryml::ConstNodeRef cfg) override;
    bool step(step_phase) override;
    bool event(SDL_Event*) override;

private:
    void _draw_main_menu();
    void _draw_overlay();
    void _sync_editor_cam_to_game();
    void _apply_override_layers();
    void _ensure_editor_cam();

    editor_p *_d;
};

}
