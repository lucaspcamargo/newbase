#include "entt/core/hashed_string.hpp"
#include <newbase/sys/voxel_world/voxel_world.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/reflection/contexts.hpp>

// ---------------------------------------------------------------------------
// RTTI
// ---------------------------------------------------------------------------

extern "C" void _rtti_init_voxel_world()
{
    using namespace nb;
    using namespace rtti;
    using entt::operator""_hs;

    entt::meta_factory<sys_voxel_world>{}
    .type("voxel_world"_hs)
    .custom<type_info>(type_info{
        .identifier = "voxel_world",
        .type_class = TYPE_CLASS_SYSTEM
    })
    .base<nb::system>();

    // func decl for later
    //.func<&sys_voxel_world::funcname>("funcname"_hs)
    //.custom<func_info>(func_info{"funcname"})

    // system factory
    entt::meta_factory<std::shared_ptr<sys_voxel_world>>{ctx_systems()}
    .type("voxel_world_shared"_hs)
    .ctor<&shared_ptr_builder<sys_voxel_world>>()
    .conv<std::shared_ptr<nb::system>>();
}
