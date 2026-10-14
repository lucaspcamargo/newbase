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
    DEPTH_32,
    COLOR_R_8_UNORM,
    COLOR_R_16_FLOAT,
    COLOR_RG_16_FLOAT,
    COLOR_RGBA_16_FLOAT,

    // SRGB formats
    // They mirror non-SRGB formats by setting MSB
    COLOR_RGBA_8888_SRGB = COLOR_RGBA_8888 | 0x80,
    COLOR_BGRA_8888_SRGB = COLOR_BGRA_8888 | 0x80,
};

// for MSAA at some later point
enum class texture_sample_count : uint8_t
{
    SAMPLES_1,
    SAMPLES_2,
    SAMPLES_4,
    SAMPLES_8
};

enum class texture_type : uint8_t
{
    INVALID,
    IMAGE_2D,
    IMAGE_3D,
    CUBEMAP
};

/// Texture state and usage flags.
enum class texture_flags : uint8_t
{
    /// Asks renderer not to release CPU-side data when uploading the texture
    KEEP_CPU = 1 << 0,

    /// Flags the texture for (re)upload by the renderer
    NEEDS_UPLOAD = 1 << 1,

    /// Denotes that the texture has been uploaded
    UPLOADED = 1 << 2,

    /// Denotes that the texture will be updated frequently
    STREAMING_HINT = 1 << 3,

    /// Denotes that the texture is a render target
    TARGET = 1 << 4,

    /// Instead of the internal data buffer, the texture uses an  SDL_Surface*
    /// to store 2D image data. Valid only for IMAGE_2D. This is for convenience.
    /// For 2D renderer compatibility, 2D rendering should favor this method.
    SDL_SURFACE = 1 << 5,

    /// Asks the renderer to generate a full mipmap chain for the texture
    GENERATE_MIPMAPS = 1 << 6,

    /// Generate mipmaps after the texture has been rendered to.
    GENERATE_MIPMAPS_DEFERRED = 1 << 7,
};

constexpr texture_flags operator|(texture_flags lhs, texture_flags rhs)
{
    return static_cast<texture_flags>(static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
}

constexpr texture_flags operator&(texture_flags lhs, texture_flags rhs)
{
    return static_cast<texture_flags>(static_cast<uint8_t>(lhs) & static_cast<uint8_t>(rhs));
}

constexpr texture_flags operator~(texture_flags value)
{
    return static_cast<texture_flags>(~static_cast<uint8_t>(value));
}

constexpr texture_flags& operator|=(texture_flags& lhs, texture_flags rhs)
{
    lhs = lhs | rhs;
    return lhs;
}

constexpr texture_flags& operator&=(texture_flags& lhs, texture_flags rhs)
{
    lhs = lhs & rhs;
    return lhs;
}

constexpr bool has_flag(texture_flags flags, texture_flags flag)
{
    return (flags & flag) == flag;
}

}
