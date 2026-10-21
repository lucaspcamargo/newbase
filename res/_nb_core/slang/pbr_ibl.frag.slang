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
    float  occlusion_strength;
    float  padding0;
    float  prefiltered_max_lod;
    float3 padding1;
};

[[vk::binding(2, 3)]]
ConstantBuffer<PBRMaterialParams> g_material : register(b2);

[[vk::binding(0, 2)]] Sampler2D s_base_color;
[[vk::binding(1, 2)]] Sampler2D s_normal;
[[vk::binding(2, 2)]] Sampler2D s_metallic_roughness;
[[vk::binding(3, 2)]] Sampler2D s_occlusion;
[[vk::binding(4, 2)]] Sampler2D s_emissive;
[[vk::binding(5, 2)]] SamplerCube<float4> s_irradiance;
[[vk::binding(6, 2)]] SamplerCube<float4> s_prefiltered;
[[vk::binding(7, 2)]] Sampler2D s_brdf_lut;
[[vk::binding(8, 2)]] Sampler2DArray s_shadows_point;


[shader("fragment")]
float4 main(VSOutput input) : SV_Target
{
    float4 tex_color = s_base_color.Sample(input.uv);
    float3 albedo = tex_color.rgb * g_material.base_color_factor.rgb;
    float alpha = tex_color.a * g_material.base_color_factor.a;

    float4 mr_sample = s_metallic_roughness.Sample(input.uv);
    float roughness = clamp(mr_sample.g * g_material.roughness_factor, 0.05, 1.0);
    float metallic = clamp(mr_sample.b * g_material.metallic_factor, 0.0, 1.0);

    float3 N = normalize(input.world_norm);
    float3 T = normalize(input.world_tangent - dot(input.world_tangent, N) * N);
    float3 B = normalize(cross(N, T) * input.bitangent_sign);
    float3x3 TBN = float3x3(T, B, N);
    float3 tangent_normal = s_normal.Sample(input.uv).rgb * 2.0 - 1.0;
    tangent_normal.xy *= g_material.normal_scaling;
    N = normalize(mul(tangent_normal, TBN));

    float3 V = normalize(g_scene.camera_pos - input.world_pos);
    float NdotV = max(dot(N, V), 0.000001);

    float3 F0 = lerp(float3(0.04), albedo, metallic);

    float3 direct = pbr_lighting(g_scene, g_model,
                                 input.world_pos, N,
                                 albedo, metallic, roughness,
                                 s_shadows_point, input.position.xy);

    // since we have IBL, global scene ambient is ignored here

    float raw_ao = s_occlusion.Sample(input.uv).r;
    float ao = 1.0 + g_material.occlusion_strength * (raw_ao - 1.0);

    float3 ambient_F = fresnel_schlick_roughness(NdotV, F0, roughness);
    float3 ambient_kD = (1.0 - ambient_F) * (1.0 - metallic);
    float3 diffuse_ibl = s_irradiance.Sample(N).rgb * albedo / PI;
    float3 reflection = reflect(-V, N);
    float3 prefiltered = s_prefiltered.SampleLevel(
        reflection, roughness * g_material.prefiltered_max_lod).rgb;
    float2 brdf = s_brdf_lut.Sample(float2(NdotV, roughness)).rg;
    float3 specular_ibl = prefiltered * (ambient_F * brdf.x + brdf.y);
    float3 ambient = (ambient_kD * diffuse_ibl + specular_ibl) * ao;

    float3 emissive = s_emissive.Sample(input.uv).rgb * g_material.emissive_factor;
    float3 final_color = ambient + emissive + direct;
    float3 display_color = max(final_color, 0.0);
    display_color = tone_map_pbr_neutral(display_color);
    return float4(linear_to_srgb(display_color), alpha);
}
