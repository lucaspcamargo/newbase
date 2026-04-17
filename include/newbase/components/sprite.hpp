#pragma once

#include <newbase/res/sprite.hpp>
#include <glm/vec4.hpp>
#include <memory>

namespace nb {
    struct csprite {
        std::shared_ptr<rsprite> spr;
        bool visible { true };
        glm::vec4 color { 1.f, 1.f, 1.f, 1.f };  // RGBA modulation

        static void _ensure_rtti();
    };

}