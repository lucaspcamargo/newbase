#include "./inc/tonemapping.slangh"

[[vk::binding(0, 2)]]
SamplerCube<float4> s_environment;

struct SkyBoxParams
{
    float sampleLevel;
    float3 _pad;
};


[[vk::binding(0, 3)]]
ConstantBuffer<SkyBoxParams> g_material : register(b2);

struct VSOutput
{
    float4 position : SV_Position;
    float3 direction : TEXCOORD0;
};

[shader("fragment")]
float4 main(VSOutput input) : SV_Target
{
    float3 display_color = s_environment.SampleLevel(normalize(input.direction),
        g_material.sampleLevel).rgb;
    display_color = tone_map_pbr_neutral(display_color);
    return float4(linear_to_srgb(display_color), 1.0);
}
