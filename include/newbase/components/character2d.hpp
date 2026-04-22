#pragma once

#include <newbase/utility/glm.hpp>
#include <cstdint>

namespace nb {

    struct ccharacter2d {
        float capsule_radius      {16.0f};
        float capsule_half_height {24.0f};

        glm::vec2 velocity        {0.0f, 0.0f};
        float     gravity_scale   {1.0f};

        uint64_t category_bits {0x0000000000000002};
        uint64_t mask_bits     {0xffffffffffffffff};

        float push_force {1.0f};  // velocity transfer coefficient for pushing dynamic bodies (1.0 = full speed transfer)

        bool grounded {false};

        static void _ensure_rtti();
    };

}
