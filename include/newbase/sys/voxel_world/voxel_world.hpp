#pragma once

#include <newbase/system.hpp>

namespace nb
{

struct sys_voxel_world_p;

/// `voxel_world` system: render Minecraft-style worlds with newbase!
/// Uses the `stb_voxel_render` library internally. It does not use greedy
/// face merging, but this is the tradeoff for a simpler API and very optimal
/// vetex data formats.
class sys_voxel_world : public system
{
public:

    using block_type = uint8_t;

    static constexpr int CLUSTER_W = 32;
    static constexpr int CLUSTER_H = 32;
    static constexpr int CLUSTER_D = 32;
    static constexpr int CLUSTER_VOX_COUNT = CLUSTER_W * CLUSTER_H * CLUSTER_D;

    sys_voxel_world();
    ~sys_voxel_world();

    SDL_InitFlags sdl_subsystems(ryml::ConstNodeRef) override { return 0; }
    entt::id_type metatype_id() override { return entt::hashed_string{"voxel_world"}.value(); }

    bool init(ryml::ConstNodeRef cfg) override;
    bool step(step_phase phase) override;
    bool event(SDL_Event*) override { return true; }

private:
    std::unique_ptr<sys_voxel_world_p> _d;
};

}
