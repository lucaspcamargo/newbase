#pragma once

#include <newbase/system.hpp>
#include <newbase/res/tilemap.hpp>
#include <glm/vec2.hpp>
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

    // Returns the value of a custom tile property at a world-space position on a named tile layer.
    // Returns an empty meta_any if the tile is empty, out of bounds, or has no such property.
    entt::meta_any get_tile_property_at(entt::entity map_entity, glm::vec2 world_pos,
                                        const std::string& layer_name,
                                        const std::string& prop_name) const;

    // Convenience: returns a bool tile property as a plain bool (false if absent or not bool).
    // Accepts a map resource id (same as get_layer_object_count) instead of an entity.
    bool tile_bool_property_at(entt::id_type map_res_id, glm::vec2 world_pos,
                               std::string layer_name, std::string prop_name) const;
};

} // namespace nb
