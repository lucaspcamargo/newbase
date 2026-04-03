#pragma once

#include <newbase/res/sprite.hpp>
#include <memory>

namespace nb {
    struct csprite {
        std::shared_ptr<rsprite> spr;
        
        static void _ensure_rtti();
    };

}