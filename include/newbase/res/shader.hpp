#pragma once

#include "entt/core/fwd.hpp"
#include <newbase/res/resource.hpp>
#include <newbase/render/shader.hpp>

namespace nb
{

/**
 * Our shader resource type.
 * This is meant to be renderer-agnostic, and be able to load
 * binary data too, with reflection information.
 * For now we'll focus on HLSL compiled via SDL_shadercross.
 */
class rshader : public resource
{
public:
    rshader(entt::id_type id) :
        resource(id, entt::hashed_string{"rshader"}.value()) {}
    ~rshader() override;

    render::shader_format format {};
    render::shader_reflection meta {};
    std::vector<uint8_t> data {};

    // this pattern is getting repetitive
    // let's create a "resource_managed" class or something
    void *rptr {nullptr};
    void (*on_destroyed)(rshader&, void*) {nullptr};
    void *on_destroyed_uptr {nullptr};

protected:
    bool do_load() override;
};

}
