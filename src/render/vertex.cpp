#include <newbase/render/vertex.hpp>
#include <cstddef>
#include <algorithm>
#include <cctype>
#include <string>


using namespace nb;
using namespace nb::render;


const vertex_type_descriptor::descriptor_map& vertex_type_descriptor::descriptor_table()
{
    static const descriptor_map ret = {
        {vertex_type::POS_NORM_UV_COL_3D, {
            vertex_type::POS_NORM_UV_COL_3D,
            sizeof(vertex3d),
            {// attributes
                {vertex_attribute_type::POSITION, vertex_data_type::FLOAT3, (uint32_t)offsetof(vertex3d,pos)},
                {vertex_attribute_type::NORMAL,   vertex_data_type::FLOAT3, (uint32_t)offsetof(vertex3d,norm)},
                {vertex_attribute_type::TEXCOORD, vertex_data_type::FLOAT2, (uint32_t)offsetof(vertex3d,uv)},
                {vertex_attribute_type::COLOR,    vertex_data_type::FLOAT4, (uint32_t)offsetof(vertex3d,col)}
            }
        }},
        {vertex_type::POS_NORM_UV_COL_TANG_3D, {
            vertex_type::POS_NORM_UV_COL_TANG_3D,
            sizeof(vertex3d_tang),
            {// attributes
                {vertex_attribute_type::POSITION, vertex_data_type::FLOAT3, (uint32_t)offsetof(vertex3d_tang,pos)},
                {vertex_attribute_type::NORMAL,   vertex_data_type::FLOAT3, (uint32_t)offsetof(vertex3d_tang,norm)},
                {vertex_attribute_type::TEXCOORD, vertex_data_type::FLOAT2, (uint32_t)offsetof(vertex3d_tang,uv)},
                {vertex_attribute_type::COLOR,    vertex_data_type::FLOAT4, (uint32_t)offsetof(vertex3d_tang,col)},
                {vertex_attribute_type::TANGENT,  vertex_data_type::FLOAT4, (uint32_t)offsetof(vertex3d_tang,tang)}
            }
        }},
        {vertex_type::POS_3D, {
            vertex_type::POS_3D,
            sizeof(vertex3d_only_pos),
            {// attributes
                {vertex_attribute_type::POSITION, vertex_data_type::FLOAT3, 0u}
            }
        }},
        {vertex_type::STANDARD_2D, {
            vertex_type::STANDARD_2D,
            sizeof(vertex2d),
            {// attributes
                {vertex_attribute_type::POS_2D,   vertex_data_type::FLOAT2, 0u},
                {vertex_attribute_type::COLOR,    vertex_data_type::FLOAT4, (uint32_t)offsetof(vertex2d,color)},
                {vertex_attribute_type::TEXCOORD, vertex_data_type::FLOAT2, (uint32_t)offsetof(vertex2d,uv)},
            }
        }},
    };
    return ret;
}


vertex_attribute_type conv::name_to_vertex_attribute(std::string_view name)
{
    // Case-insensitive / normalized string comparison
    std::string s(name);
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::toupper(c); });

    // glTF 2.0 & common shader naming conventions
    if (s == "POSITION" || s == "IN_POSITION" || s == "IN_POS" || s == "A_POSITION" || s == "POS")
        return vertex_attribute_type::POSITION;

    if (s == "NORMAL" || s == "IN_NORMAL" || s == "IN_NORM" || s == "A_NORMAL" || s == "NORM")
        return vertex_attribute_type::NORMAL;

    if (s == "TEXCOORD" || s == "TEXCOORD_0" || s == "IN_TEXCOORD" || s == "IN_UV" || s == "A_UV" || s == "UV")
        return vertex_attribute_type::TEXCOORD;

    if (s == "COLOR" || s == "COLOR_0" || s == "IN_COLOR" || s == "IN_COL" || s == "A_COLOR" || s == "COL")
        return vertex_attribute_type::COLOR;

    if (s == "TANGENT" || s == "IN_TANGENT" || s == "IN_TANG" || s == "A_TANGENT" || s == "TANG")
        return vertex_attribute_type::TANGENT;

    if (s == "POS_2D" || s == "IN_POS2D")
        return vertex_attribute_type::POS_2D;

    return vertex_attribute_type::INVALID;
}


uint32_t conv::get_vertex_data_type_size(vertex_data_type dt)
{
    switch (dt) {
        case vertex_data_type::FLOAT2:
            return 4*2;
        case vertex_data_type::FLOAT3:
            return 4*3;
        case vertex_data_type::FLOAT4:
            return 4*4;
        case vertex_data_type::INVALID:
            return 0;
    }
    return 0;
}
