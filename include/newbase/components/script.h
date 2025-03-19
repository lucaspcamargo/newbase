#pragma once

#include <newbase/res/script.h>
#include <entt/resource/resource.hpp>
#include <glm/glm.hpp>

namespace nb {
    struct cscript {
        entt::resource<rscript> script {};
        void * state {nullptr};
    };
}