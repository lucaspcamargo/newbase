#pragma once

#include <cstdint>

namespace nb {

struct clayers {
    static constexpr uint32_t MASK_DEFAULT = 0x1;
    static constexpr uint32_t MASK_ALL     = 0xFFFFFFFF;

    uint32_t mask { MASK_DEFAULT };

    static void _ensure_rtti();
};

}
