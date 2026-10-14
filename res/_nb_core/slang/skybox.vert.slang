#include "./inc/std_uniforms.slangh"

[[vk::binding(0, 1)]]
ConstantBuffer<SceneUniforms> g_scene : register(b0);

[[vk::binding(1, 1)]]
ConstantBuffer<ModelUniforms> g_model : register(b1);

struct VSInput
{
    [[vk::location(0)]] float3 position : POSITION;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 direction : TEXCOORD0;
};

[shader("vertex")]
VSOutput main(VSInput input)
{
    VSOutput output;
    float4 world_position = mul(g_model.model, float4(input.position, 1.0));
    output.direction = world_position.xyz;

    float3 view_position = mul((float3x3)g_scene.view, world_position.xyz);
    float4 clip_position = mul(g_scene.proj, float4(view_position, 1.0));
    clip_position.z = clip_position.w * 0.9999; // when render::pipeline params has depth_clip disabled, we can just set to w I suppose
    output.position = clip_position;
    return output;
}
