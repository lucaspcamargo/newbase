#pragma once

#include <cstdint>


// Some types used in rendering-related structures,
// especially enums

namespace nb::render
{
    /**
     * Blend mode to use in drawing commands.
     * We keep compatibility here with SDL_BlendMode for simplicity.
     */
    enum class blendmode : uint32_t
    {
        NONE    = 0x0,
        BLEND   = 0x1,
        ADD     = 0x2,
        MOD     = 0x4,
        MUL     = 0x8,
        INVALID = 0x7FFFFFFF
    };
};
