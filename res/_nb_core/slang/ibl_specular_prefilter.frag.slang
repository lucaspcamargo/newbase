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
SamplerCube<float4> s_environment;

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

float radical_inverse_vdc(uint bits)
{
    bits = (bits << 16) | (bits >> 16);
    bits = ((bits & 0x55555555) << 1) | ((bits & 0xAAAAAAAA) >> 1);
    bits = ((bits & 0x33333333) << 2) | ((bits & 0xCCCCCCCC) >> 2);
    bits = ((bits & 0x0F0F0F0F) << 4) | ((bits & 0xF0F0F0F0) >> 4);
    bits = ((bits & 0x00FF00FF) << 8) | ((bits & 0xFF00FF00) >> 8);
    return float(bits) * 2.3283064365386963e-10;
}

float2 hammersley(uint index, uint count)
{
    return float2(float(index) / float(count), radical_inverse_vdc(index));
}

float3 importance_sample_ggx(float2 xi, float roughness, float3 normal)
{
    float a = roughness * roughness;
    float phi = 2.0 * PI * xi.x;
    float cos_theta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sin_theta = sqrt(max(0.0, 1.0 - cos_theta * cos_theta));
    float3 half_vector = float3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);
    float3 up = abs(normal.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(up, normal));
    float3 bitangent = cross(normal, tangent);
    return normalize(tangent * half_vector.x + bitangent * half_vector.y + normal * half_vector.z);
}

float distribution_ggx(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH2 = NdotH * NdotH;
    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denom * denom, 0.000001);
}

[shader("fragment")]
float4 main(VSOutput input) : SV_Target
{
    float3 normal = normalize(cube_direction(g_integration.face_index, input.uv));
    float3 view = normal;
    float roughness = g_integration.mip_count > 1
        ? float(g_integration.mip_level) / float(g_integration.mip_count - 1)
        : 0.0;
    uint sample_count = max(g_integration.sample_count, 1);

    float3 prefiltered = float3(0.0, 0.0, 0.0);
    float total_weight = 0.0;
    for(uint i = 0; i < sample_count; ++i)
    {
        float2 xi = hammersley(i, sample_count);
        float3 half_vector = importance_sample_ggx(xi, max(roughness, 0.001), normal);
        float3 light = normalize(2.0 * dot(view, half_vector) * half_vector - view);
        float n_dot_l = max(dot(normal, light), 0.0);
        if(n_dot_l > 0.0)
        {
            float n_dot_h = max(dot(normal, half_vector), 0.0);
            float h_dot_v = max(dot(half_vector, view), 0.0);
            float D = distribution_ggx(n_dot_h, roughness);
            float pdf = (D * n_dot_h / (4.0 * h_dot_v)) + 0.0001;

            float saTexel = 4.0 * PI / (6.0 * g_integration.source_resolution * g_integration.source_resolution);
            float saSample = 1.0 / (float(sample_count) * pdf);
            float lod = roughness == 0.0 ? 0.0 : 0.5 * log2(saSample / saTexel);
            lod = clamp(lod, 0.0, float(max(g_integration.source_mip_count, 1) - 1));

            prefiltered += s_environment.SampleLevel(light, lod).rgb * n_dot_l;
            total_weight += n_dot_l;
        }
    }

    return float4(prefiltered / max(total_weight, 0.0001), 1.0);
}
