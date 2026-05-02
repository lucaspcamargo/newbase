#pragma once

#include <newbase/system.hpp>
#include <newbase/res/tilemap.hpp>
#include <string>

namespace nb {

class tilemap_system : public system
{
public:
    tilemap_system();
    ~tilemap_system();

    SDL_InitFlags sdl_subsystems(ryml::ConstNodeRef) override { return 0; }
    entt::id_type metatype_id() override { return entt::hashed_string{"tilemap_system"}.value(); }

    bool init(ryml::ConstNodeRef cfg) override;
    bool step(step_phase phase) override;
    bool event(SDL_Event*) override { return true; }

    unsigned int get_layer_object_count(entt::id_type map_id, std::string layer_name) const;
    tilemap_object get_layer_object(entt::id_type map_id, std::string layer_name, unsigned int idx) const;
};

} // namespace nb
