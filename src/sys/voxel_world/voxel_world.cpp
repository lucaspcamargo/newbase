#include "entt/core/fwd.hpp"
#include <newbase/sys/voxel_world/voxel_world.hpp>
#include <newbase/utility/glm.hpp>

#define STB_VOXEL_RENDER_IMPLEMENTATION 1
#define STB_VOXEL_RENDER_STATIC 1
#define STBVOX_CONFIG_MODE 0
#include <stb_voxel_render.h>

using namespace nb;

// TODO support for generating cluster and geometry data in the background
// via threads

/// a block of voxels, with an associated mesh
struct voxel_cluster
{

};

/// voxel cluster coordinate
using voxel_cluster_coord = glm::ivec3;

/// std::hash secializtion for voxel cluster coordinate
template<>
struct std::hash<voxel_cluster_coord>
{
    size_t operator()(const voxel_cluster_coord &addr)
    {
        return entt::hashed_string{(const char *)&addr, sizeof(glm::ivec3)}.value();
    }
};

/// our private data struct
struct nb::sys_voxel_world_p
{
    stbvox_mesh_maker maker {};

    std::unordered_map<voxel_cluster_coord, voxel_cluster> clusters;
};

sys_voxel_world::sys_voxel_world()
    : _d {nullptr}
{
}

sys_voxel_world::~sys_voxel_world()
{
}

bool sys_voxel_world::init(ryml::ConstNodeRef cfg)
{
    _d = std::make_unique<sys_voxel_world_p>();

    stbvox_init_mesh_maker(&_d->maker);

    return true;
}

bool sys_voxel_world::step(step_phase phase)
{
    return true;
}
