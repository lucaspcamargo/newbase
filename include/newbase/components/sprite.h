#pragma once

#include <newbase/res/sprite.h>
#include <entt/resource/resource.hpp>
#include <glm/glm.hpp>

namespace nb {
    struct csprite {
        entt::resource<rsprite> spr;
    };
}