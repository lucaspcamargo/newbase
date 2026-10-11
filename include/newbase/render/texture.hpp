#pragma once

#include <cstdint>

namespace nb::render
{

// our engine-native texture format type
// only describe what we implement and use, don't try to describe
// every format of every API
enum class texture_format : uint8_t
{
    INVALID,
    COLOR_RGBA_8888,
    COLOR_BGRA_8888,
    DEPTH_16,
    DEPTH_24,
    DEPTH_32
};

// for MSAA at some later point
enum class texture_sample_count : uint8_t
{
    SAMPLES_1,
    SAMPLES_2,
    SAMPLES_4,
    SAMPLES_8
};

}
