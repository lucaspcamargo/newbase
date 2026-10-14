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

[shader("fragment")]
float4 main(VSOutput input) : SV_Target
{
    float3 normal = normalize(cube_direction(g_integration.face_index, input.uv));
    float3 up = abs(normal.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(up, normal));
    float3 bitangent = cross(normal, tangent);

    uint sample_count = max(g_integration.sample_count, 1);
    float3 irradiance = float3(0.0, 0.0, 0.0);
    for(uint i = 0; i < sample_count; ++i)
    {
        float2 xi = hammersley(i, sample_count);
        float phi = 2.0 * PI * xi.x;
        float cos_theta = sqrt(1.0 - xi.y);
        float sin_theta = sqrt(xi.y);
        float3 local_direction = float3(
            cos(phi) * sin_theta,
            sin(phi) * sin_theta,
            cos_theta);
        float3 sample_direction = normalize(
            tangent * local_direction.x +
            bitangent * local_direction.y +
            normal * local_direction.z);
        float pdf = max(cos_theta / PI, 1.0e-6);
        float sample_solid_angle = 1.0 / (float(sample_count) * pdf);
        float texel_solid_angle = 4.0 * PI /
            (6.0 * g_integration.source_resolution * g_integration.source_resolution);
        float lod = 0.5 * log2(sample_solid_angle / texel_solid_angle);
        lod = clamp(lod, 0.0, float(max(g_integration.source_mip_count, 1) - 1));
        irradiance += s_environment.SampleLevel(sample_direction, lod).rgb;
    }

    irradiance *= PI / float(sample_count);
    return float4(irradiance, 1.0);
}
