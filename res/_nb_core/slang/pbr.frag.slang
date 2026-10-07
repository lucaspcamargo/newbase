static const float PI = 3.14159265359;

struct VSOutput
{
    float4 position   : SV_Position;
    float3 world_pos  : TEXCOORD0;
    float3 world_norm : TEXCOORD1;
    float2 uv         : TEXCOORD2;
    float3 world_tangent   : TEXCOORD3;
    float  bitangent_sign  : TEXCOORD4;
};

struct SceneUniforms
{
    column_major float4x4 view;
    column_major float4x4 proj;
    column_major float4x4 view_proj;
    column_major float4x4 inv_view;
    float3   camera_pos;
    float    _pad0;
    float3   light_dir;      // Normalized direction towards light source
    float    _pad1;
    float3   light_color;    // Light radiance (color * intensity)
    float    _pad2;
};

[[vk::binding(0, 3)]]
ConstantBuffer<SceneUniforms>     g_scene    : register(b0);

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

[[vk::binding(1, 3)]]
ConstantBuffer<PBRMaterialParams> g_material : register(b2);


// SAMPLERS

[[vk::binding(0, 2)]] Sampler2D s_base_color;
[[vk::binding(1, 2)]] Sampler2D s_normal;
[[vk::binding(2, 2)]] Sampler2D s_metallic_roughness;
[[vk::binding(3, 2)]] Sampler2D s_occlusion;
[[vk::binding(4, 2)]] Sampler2D s_emissive;


// ============================================================================
// PBR MATHEMATICAL HELPER FUNCTIONS (Cook-Torrance BRDF)
// ============================================================================

// 1. Normal Distribution Function (NDF) - Trowbridge-Reitz GGX
// Calculates the statistical alignment of microfacets with the halfway vector H.
float distribution_ggx(float NdotH, float roughness)
{
    float a      = roughness * roughness;
    float a2     = a * a;
    float NdotH2 = NdotH * NdotH;

    float num   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom       = PI * denom * denom;

    return num / max(denom, 0.000001); // Prevent division by zero
}

// 2. Geometry Function - Schlick-GGX (Direct Lighting)
// Calculates self-shadowing/occlusion of microfacets.
float geometry_schlick_ggx(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0; // Direct lighting mapping

    float num   = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return num / max(denom, 0.000001);
}

// Smith's method combining Geometry obstruction for both Light and View directions
float geometry_smith(float NdotV, float NdotL, float roughness)
{
    float ggx2 = geometry_schlick_ggx(NdotV, roughness);
    float ggx1 = geometry_schlick_ggx(NdotL, roughness);

    return ggx1 * ggx2;
}

// 3. Fresnel Equation - Schlick Approximation
// Calculates the ratio of reflected light over refracted light at different angles.
float3 fresnel_schlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

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


    // View vector pointing from surface to camera
    float3 V = normalize(g_scene.camera_pos - input.world_pos);

    // Light vector pointing from surface to light source
    float3 L = normalize(g_scene.light_dir);

    // Halfway vector between View and Light
    float3 H = normalize(V + L);

    // Pre-calculate dot products used in BRDF equations
    float NdotV = max(dot(N, V), 0.000001);
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    // --- 2. Reflectance at Normal Incidence (F0) ---
    // Dielectrics (non-metals) use ~0.04 base reflectivity, metals use their albedo color
    float3 F0 = float3(0.04);
    F0 = lerp(F0, albedo, metallic);

    // --- 3. Cook-Torrance Specular BRDF Term ---

    float  D = distribution_ggx(NdotH, roughness);
    float  G = geometry_smith(NdotV, NdotL, roughness);
    float3 F = fresnel_schlick(HdotV, F0);

    // Specular BRDF numerator and denominator
    float3 numerator   = D * G * F;
    float  denominator = 4.0 * NdotV * NdotL;
    float3 specular    = numerator / max(denominator, 0.000001);

    // --- 4. Energy Conservation (Diffuse vs Specular Split) ---

    // kS equals Fresnel reflectance ratio
    float3 kS = F;

    // kD is the remaining light that gets refracted into diffuse scattering
    float3 kD = (float3(1.0) - kS) * (1.0 - metallic); // Metals have zero diffuse reflection

    // Lambertian diffuse BRDF
    float3 diffuse = kD * (albedo / PI);

    // --- 5. Accumulate Radiance ---

    // Direct radiance contribution
    float3 radiance_out = (diffuse + specular) * g_scene.light_color * NdotL;

    // Ambient Occlusion
    float raw_ao = s_occlusion.Sample(input.uv).r;
    float ao = 1.0 + g_material.occlusion_strenght * (raw_ao - 1.0);
    float3 ambient = float3(0.2) * albedo * ao;  // TODO fixed ambient

    // Emissive
    float3 emissive = s_emissive.Sample(input.uv).rgb * g_material.emissive_factor;

    float3 final_color = ambient + emissive + radiance_out;

    return float4(pow(final_color, 1.0/2.2), alpha); //float4(final_color.rgb, alpha);
}
