#pragma once

#include <cstdint>

namespace nb {

struct clayers {
    uint32_t mask { 0xFFFFFFFF };  // member of all layers by default

    static void _ensure_rtti();
};

}
