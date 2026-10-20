#include "./inc/std_uniforms.slangh"

[[vk::binding(0, 1)]]
ConstantBuffer<SceneUniforms> g_scene : register(b0);

[[vk::binding(1, 1)]]
ConstantBuffer<ModelUniforms> g_model : register(b1);

struct VSInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float2 col : TEXCOORD1;
    [[vk::location(4)]] float4 tangent : TEXCOORD2;
};

struct VSOutput
{
    float4 position : SV_Position;
};

[shader("vertex")]
VSOutput main(VSInput input)
{
    VSOutput output;
    float4 world_pos = mul(g_model.model, float4(input.position, 1.0));
    output.position = mul(g_scene.view_proj, world_pos);
    return output;
}
