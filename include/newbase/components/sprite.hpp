#pragma once

#include <newbase/res/sprite.hpp>
#include <entt/resource/resource.hpp>

namespace nb {
    struct csprite {
        entt::resource<rsprite> spr;
        
        static void _ensure_rtti();
    };

}