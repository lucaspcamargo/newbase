#pragma once

#include <newbase/utility/glm.hpp>
#include <unordered_map>
#include <string_view>
#include <vector>
#include <cstdint>

// vertex structures and types
// for 3D rendering and such

namespace nb::render
{

    enum class vertex_type : uint8_t
    {
        INVALID,
        POS_NORM_UV_COL_3D,
        POS_NORM_UV_COL_TANG_3D,
        POS_3D,
        STANDARD_2D,
        COUNT
    };

    // let's just start with a simple anf general vertex type
    struct vertex3d
    {
        glm::vec3 pos;
        glm::vec3 norm;
        glm::vec2 uv;
        glm::vec4 col;
        static constexpr vertex_type type {vertex_type::POS_NORM_UV_COL_3D};
    };

    struct vertex3d_tang
    {
        glm::vec3 pos;
        glm::vec3 norm;
        glm::vec2 uv;
        glm::vec4 col;
        glm::vec4 tang;
        static constexpr vertex_type type {vertex_type::POS_NORM_UV_COL_TANG_3D};
    };

    struct vertex3d_only_pos
    {
        glm::vec3 pos;
        static constexpr vertex_type type {vertex_type::POS_3D};
    };

    struct vertex2d
    {
        glm::vec2 pos;
        glm::vec4 color;
        glm::vec2 uv;
    };

    // a type of vertex attribute
    enum class vertex_attribute_type : uint8_t
    {
        INVALID,
        POSITION,
        NORMAL,
        TEXCOORD,
        COLOR,
        TANGENT,
        POS_2D
    };

    enum class vertex_data_type : uint8_t
    {
        INVALID,
        FLOAT4,
        FLOAT3,
        FLOAT2
    };


    // a concrete vertex attribute in a vertex type
    // semantic meaning, used for routing correct data in the proper
    struct vertex_attribute_descriptor
    {
       vertex_attribute_type attr {vertex_attribute_type::INVALID};
       vertex_data_type format {vertex_data_type::INVALID};
       uint32_t offset {0};
    };

    struct vertex_type_descriptor
    {
        vertex_type type;
        uint32_t size;
        std::vector<vertex_attribute_descriptor> attributes;

        // global static data
        using descriptor_map = std::unordered_map<vertex_type, vertex_type_descriptor>;
        static const descriptor_map& descriptor_table();
    };

    namespace conv
    {
        vertex_attribute_type name_to_vertex_attribute(std::string_view name);
        uint32_t get_vertex_data_type_size(vertex_data_type dt);
    }

}
