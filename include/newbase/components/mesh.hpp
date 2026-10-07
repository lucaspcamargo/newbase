#pragma once

#include <newbase/res/mesh.hpp>
#include <newbase/res/material.hpp>
#include <memory>
#include <array>

namespace nb {

    struct cmesh {
        std::shared_ptr<rmesh> mesh;

        static constexpr int MAX_MATERIALS = 8;
        std::array<std::shared_ptr<rmaterial>, MAX_MATERIALS> materials;

        static void _ensure_rtti();
    };

}
