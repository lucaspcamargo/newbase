#include "./inc/std_uniforms.slangh"

[[vk::binding(0, 3)]]
ConstantBuffer<SceneUniforms> g_scene : register(b0);

[[vk::binding(1, 3)]]
ConstantBuffer<ModelUniforms> g_model : register(b1);

struct VSOutput
{
    float4 position : SV_Position;
};

[shader("fragment")]
float4 main(VSOutput input) : SV_Target
{
    return float4(0.0);
}
