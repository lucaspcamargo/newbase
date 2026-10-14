#include "./inc/std_uniforms.slangh"

[[vk::binding(0, 3)]]
ConstantBuffer<SceneUniforms> g_scene : register(b0);

struct MatCapMaterialUniforms
{
    float4 base_color_factor;
    float matcap_intensity;
    float normal_scaling;
    float occlusion_strength;
    float padding;
};

[[vk::binding(1, 3)]]
ConstantBuffer<MatCapMaterialUniforms> g_material : register(b2);

[[vk::binding(0, 2)]]
Sampler2D s_matcap;
[[vk::binding(1, 2)]]
Sampler2D s_normal;
[[vk::binding(2, 2)]]
Sampler2D s_occlusion;

struct VSOutput
{
    float4 position : SV_Position;
    float3 world_normal : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float3 world_tangent : TEXCOORD2;
    float tangent_sign : TEXCOORD3;
};

[shader("fragment")]
float4 main(VSOutput input) : SV_Target
{
    float3 world_normal = normalize(input.world_normal);
    float3 tangent = normalize(input.world_tangent -
        dot(input.world_tangent, world_normal) * world_normal);
    float3 bitangent = normalize(cross(world_normal, tangent) * input.tangent_sign);
    float3 tangent_normal = s_normal.Sample(input.uv).rgb * 2.0 - 1.0;
    tangent_normal.xy *= g_material.normal_scaling;
    float3 normal = normalize(mul(tangent_normal,
        float3x3(tangent, bitangent, world_normal)));
    normal = normalize(mul((float3x3)g_scene.view, normal));
    float2 uv = normal.xy * float2(0.5, -0.5) + 0.5;
    float4 matcap = s_matcap.Sample(uv);
    float ao_sample = s_occlusion.Sample(input.uv).r;
    float ao = 1.0 + g_material.occlusion_strength * (ao_sample - 1.0);
    return float4(matcap.rgb * g_material.base_color_factor.rgb *
                      g_material.matcap_intensity * ao,
                  matcap.a * g_material.base_color_factor.a);
}
