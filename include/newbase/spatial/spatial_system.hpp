#pragma once

#include <newbase/system.hpp>
#include <entt/entt.hpp>

namespace nb {

class spatial_system : public system
{
public:
    spatial_system();
    ~spatial_system();

    SDL_InitFlags sdl_subsystems(ryml::ConstNodeRef) override { return 0; }
    entt::id_type metatype_id() override { return entt::hashed_string{"spatial_system"}.value(); }

    bool init(ryml::ConstNodeRef cfg) override;
    bool step(step_phase phase) override;
    bool event(SDL_Event*) override { return true; }

    void on_scene_change() override;

    static void on_structure_destroy(entt::registry &reg, entt::entity e);

private:
    bool m_needs_reconnect { true };
    entt::connection m_on_destroy_conn;

    void connect_signals();
};

} // namespace nb
