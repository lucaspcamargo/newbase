static const float PI = 3.14159265359;

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

struct IntegrationUniforms
{
    uint face_index;
    uint mip_level;
    uint mip_count;
    uint sample_count;
    float source_resolution;
    uint source_mip_count;
    float2 padding;
};

[[vk::binding(0, 3)]]
ConstantBuffer<IntegrationUniforms> g_integration : register(b0);

[[vk::binding(0, 2)]]
Sampler2D s_equirectangular;

float3 cube_direction(uint face, float2 uv)
{
    float2 p = uv * 2.0 - 1.0;
    if(face == 0) return float3( 1.0, -p.y, -p.x);
    if(face == 1) return float3(-1.0, -p.y,  p.x);
    if(face == 2) return float3( p.x,  1.0,  p.y);
    if(face == 3) return float3( p.x, -1.0, -p.y);
    if(face == 4) return float3( p.x, -p.y,  1.0);
    return float3(-p.x, -p.y, -1.0);
}

[shader("fragment")]
float4 main(VSOutput input) : SV_Target
{
    float3 direction = normalize(cube_direction(g_integration.face_index, input.uv));
    float2 source_uv;
    source_uv.x = atan2(direction.z, direction.x) / (2.0 * PI) + 0.5;
    source_uv.y = 0.5 - asin(clamp(direction.y, -1.0, 1.0)) / PI;
    return s_equirectangular.Sample(source_uv);
}
