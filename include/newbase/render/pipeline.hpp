#pragma once

#include <newbase/render/types.hpp>
#include <newbase/render/vertex.hpp>
#include <newbase/render/pass.hpp>

// our engine-native pipeline descriptor type

// when being requested concrete rendering data, a material_controller will provide data
// (shaders, rasterization states, uniforms) according to standard pass types, vertex format,
// and its own shader_variant info (derived internally from material params).
// the data that can change per material instance (uniforms, texture bindings),
// except for the resulting shader_variant, is not part of the pipeline

// so, the combination:
//   "material_type + pass_type + vertex_format + shader_variant + raster config"
// defines all of the gfx pipeline info in one go (shaders, vert attrs, raster config)

// well, almost. pass_target_info is also part of it, but does not change with
// the material or mesh info

// That said, materialization of this descriptor is the responsibility of a
// specific renderer's pipeline cache. If it needs a feature not expressed here,
// we make changes to the types/members of the engine descriptor itself until it
// fits, aiming for other renderers to support the new fields, or act the same

namespace nb::render
{

struct pipeline_descriptor // NOTE keep tightly-packed pls
{
    // common render target information
    pass_target_info targets {};

    // shader and vertex
    material_variant shader_variant {0};
    material_type mat_type {material_type::INVALID};
    vertex_type vertex_fmt {vertex_type::INVALID};

    // common rasterization and blend information
    pipeline_params params {};

    // hasing and comparison
    using hash_t = uint32_t;

    [[nodiscard]] inline hash_t hash() const noexcept {
        static_assert(sizeof(hash_t) == 4, "This is 32-bit fnv-1a, change algorithm if you change hash type");
        static_assert(std::is_same<hash_t, uint32_t>(), "This is 32-bit fnv-1a, change algorithm if you change hash type");
        constexpr hash_t FNV_OFFSET_BASIS = 2166136261u;
        constexpr hash_t FNV_PRIME        = 16777619u;

        const auto* bytes = reinterpret_cast<const uint8_t*>(this);
        hash_t hash = FNV_OFFSET_BASIS;

        for (size_t i = 0; i < sizeof(pipeline_descriptor); ++i) {
            hash ^= bytes[i];
            hash *= FNV_PRIME;
        }

        return hash;
    }

    inline bool operator==(const pipeline_descriptor& other) const noexcept {
        // Fast byte-for-byte comparison for cache hits
        return std::memcmp(this, &other, sizeof(pipeline_descriptor)) == 0;
    }
};

static_assert(sizeof(pipeline_descriptor)==14, "Make sure we notice if size changes");
static_assert(alignof(pipeline_descriptor)==2, "Make sure we notice if alignment changes");

}

