#pragma once

#include <cstdint>
#include <string>
#include <vector>

// our native shader structures
// used mostly for reflection data
// reflection data can be extracted from source compiles,
// or baked as a header into precompiled shader
// binaries, when we get to it

namespace nb::render
{

enum class shader_stage : uint8_t {
    VERTEX,
    FRAGMENT,
    COMPUTE
};

enum class shader_format : uint8_t {
    SPIRV,
    DXIL,
    MSL,
    GLSL
};

enum class shader_data_type : uint8_t {
    FLOAT1, FLOAT2, FLOAT3, FLOAT4,
    INT1,   INT2,   INT3,   INT4,
    MAT4X4
};

struct shader_uniform_member {
    std::string name;
    uint32_t offset {0};
    uint32_t size {0};
    shader_data_type type {shader_data_type::FLOAT1};
    uint32_t array_element_count {1};
};

struct shader_uniform_buffer_desc {
    std::string name;
    uint32_t slot {0};
    uint32_t size_bytes {0};
    std::vector<shader_uniform_member> members;
};

struct shader_texture_binding_desc {
    std::string name;
    uint32_t slot {0};
    bool is_sampler {true};
};

struct shader_vertex_attribute_desc {
    std::string name;
    uint32_t location {0};
    shader_data_type type {shader_data_type::FLOAT3};
};

struct shader_reflection {
    shader_stage stage {shader_stage::VERTEX};

    // abstract resource counts (maps to SDL_GPU or WebGL requirements)
    uint32_t num_samplers {0};
    uint32_t num_uniform_buffers {0};
    uint32_t num_storage_textures {0};
    uint32_t num_storage_buffers {0}; // for compute later

    // metadata for materials and layout validation
    std::vector<shader_uniform_buffer_desc> uniform_buffers;
    std::vector<shader_texture_binding_desc> textures;
    std::vector<shader_vertex_attribute_desc> attributes; // vertex only
};

}
