#pragma once

#include <cstdint>

// general rendering configuration, renderer-agnostic

namespace nb::render
{

    struct config2d
    {

    };

    struct config3d
    {
        uint32_t point_shadow_resolution    {512};
        uint32_t point_shadow_layers_init   {4};
        uint32_t point_shadow_layers_max    {32};

        uint32_t dir_shadow_resolution      {1024};
        uint32_t dir_shadow_layers_init     {2};
        uint32_t dir_shadow_layers_max      {8};
    };

    struct config
    {
        config2d cfg2d;
        config3d cfg3d;
    };

}
