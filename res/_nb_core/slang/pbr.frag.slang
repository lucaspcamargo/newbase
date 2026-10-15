#include "./inc/std_uniforms.slangh"
#include "./inc/tonemapping.slangh"
#include "./inc/pbr_funcs.slangh"
#include "./inc/pbr_lighting.slangh"

struct VSOutput
{
    float4 position   : SV_Position;
    float3 world_pos  : TEXCOORD0;
    float3 world_norm : TEXCOORD1;
    float2 uv         : TEXCOORD2;
    float3 world_tangent   : TEXCOORD3;
    float  bitangent_sign  : TEXCOORD4;
};

[[vk::binding(0, 3)]]
ConstantBuffer<SceneUniforms> g_scene : register(b0);

[[vk::binding(1, 3)]]
ConstantBuffer<LitModelUniforms> g_model : register(b1);

struct PBRMaterialParams
{
    float4 base_color_factor;
    float3 emissive_factor;
    float  metallic_factor;
    float  roughness_factor;
    float  normal_scaling;
    float  occlusion_strenght;
    float _pad0;
};

[[vk::binding(2, 3)]]
ConstantBuffer<PBRMaterialParams> g_material : register(b2);


// SAMPLERS

[[vk::binding(0, 2)]] Sampler2D s_base_color;
[[vk::binding(1, 2)]] Sampler2D s_normal;
[[vk::binding(2, 2)]] Sampler2D s_metallic_roughness;
[[vk::binding(3, 2)]] Sampler2D s_occlusion;
[[vk::binding(4, 2)]] Sampler2D s_emissive;




// ============================================================================
// FRAGMENT SHADER
// ============================================================================

[shader("fragment")]
float4 main(VSOutput input) : SV_Target
{
    // --- 1. Surface Inputs & Setup ---

    // Sample texture and combine with uniform base color factor
    float4 tex_color = s_base_color.Sample(input.uv);
    float3 albedo    = tex_color.rgb * g_material.base_color_factor.rgb;
    float  alpha     = tex_color.a * g_material.base_color_factor.a;

    float4 mr_sample = s_metallic_roughness.Sample(input.uv);
    float roughness = clamp(mr_sample.g * g_material.roughness_factor, 0.05, 1.0);
    float metallic  = clamp(mr_sample.b * g_material.metallic_factor,  0.0,  1.0);

    float3 N = normalize(input.world_norm);
    float3 T = normalize(input.world_tangent - dot(input.world_tangent, N) * N); // G-S ortho
    float3 B = normalize(cross(N, T) * input.bitangent_sign);
    float3x3 TBN = float3x3(T, B, N);
    float3 tan_normal = s_normal.Sample(input.uv).rgb * 2.0 - 1.0;
    tan_normal.xy *= g_material.normal_scaling; // normal scaling factor to XY
    N = normalize(mul(tan_normal, TBN)); // tangent Space to World Space


    // PBR direct lighting
    float3 radiance_out = pbr_lighting(g_scene, g_model, input.world_pos, N, albedo, metallic, roughness);

    // Ambient Occlusion
    float raw_ao = s_occlusion.Sample(input.uv).r;
    float ao = 1.0 + g_material.occlusion_strenght * (raw_ao - 1.0);
    float3 ambient = g_scene.light_data.ambient * albedo * ao;

    // Emissive
    float3 emissive = s_emissive.Sample(input.uv).rgb * g_material.emissive_factor;

    float3 display_color = ambient + emissive + radiance_out;

    display_color = tone_map_pbr_neutral(display_color);
    return float4(linear_to_srgb(display_color), alpha); //float4(g_model.light_count * 0.1);//
}
