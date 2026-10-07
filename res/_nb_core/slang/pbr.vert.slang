// --- Uniform Buffers ---

// Per-frame / Per-pass scene parameters
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

// Per-draw object transform
struct ModelUniforms
{
    column_major float4x4 model;
    column_major float4x4 normal_matrix;  // Transpose(Inverse(Model)) to scale normals correctly
};


[[vk::binding(0, 1)]]
ConstantBuffer<SceneUniforms>     g_scene    : register(b0);


[[vk::binding(1, 1)]]
ConstantBuffer<ModelUniforms>     g_model    : register(b1);

struct VSInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal   : NORMAL;
    [[vk::location(2)]] float2 uv       : TEXCOORD0;
    [[vk::location(3)]] float4 col      : TEXCOORD1; // IGNORED (for now)
    [[vk::location(4)]] float4 tangent  : TEXCOORD2;
};

struct VSOutput
{
    float4 position   : SV_Position;
    float3 world_pos  : TEXCOORD0;
    float3 world_norm : TEXCOORD1;
    float2 uv         : TEXCOORD2;
    float3 world_tangent   : TEXCOORD3;
    float  bitangent_sign  : TEXCOORD4;
};

// ============================================================================
// VERTEX SHADER
// ============================================================================

[shader("vertex")]
VSOutput main(VSInput input)
{
    VSOutput output;

    // Transform position from local space to world space
    float4 world_pos = mul(g_model.model, float4(input.position, 1.0));
    output.world_pos = world_pos.xyz;

    // Transform position to clip space for rasterization
    output.position = mul(g_scene.view_proj, world_pos);

    // Transform normal to world space (using 3x3 subset of normal_matrix)
    output.world_norm = normalize(mul((float3x3)g_model.normal_matrix, input.normal));

    // Apply material UV tiling and offset
    output.uv = input.uv;

    // Transform tangent vector (XYZ) to world space using model matrix
    output.world_tangent = normalize(mul((float3x3)g_model.model, input.tangent.xyz));

    // Pass through the bitangent sign (+1.0 or -1.0) from glTF tangent attribute
    output.bitangent_sign = input.tangent.w;

    return output;
}
