// ============================================================================
// Uniform Layouts (Slots 0 - 2)
// ============================================================================

struct SceneUniforms {
    float4x4 view;
    float4x4 projection;
    float4x4 view_projection;
    float3   camera_pos;
    float    pad0;
};
[[vk::binding(0, 0)]] cbuffer u_scene : register(b0) { SceneUniforms scene; }

struct ModelUniforms {
    float4x4 world;
    float4x4 normal_matrix; // Inverse-transpose of World 3x3
};
[[vk::binding(0, 1)]] cbuffer u_model : register(b1) { ModelUniforms model; }

struct MatCapMaterialUniforms {
    float4 base_color_factor; // Tint & opacity multiplier
    float  matcap_intensity;  // Blend factor
    float3 pad;
};
[[vk::binding(0, 2)]] cbuffer u_material : register(b2) { MatCapMaterialUniforms material; }

// ============================================================================
// Textures & Samplers
// ============================================================================

[[vk::binding(0, 3)]] Texture2D    t_matcap : register(t0);
[[vk::binding(1, 3)]] SamplerState s_matcap : register(s0);

// ============================================================================
// Vertex & Fragment Interfaces
// ============================================================================

struct VertexInput {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
};

struct VertexOutput {
    float4 sv_position : SV_Position;
    float3 view_normal : NORMAL;
};

// ============================================================================
// Shader Entries
// ============================================================================

[shader("vertex")]
VertexOutput VSMain(VertexInput input) {
    VertexOutput output;

    // 1. Transform vertex to Clip Space
    float4 world_pos = mul(model.world, float4(input.position, 1.0));
    output.sv_position = mul(scene.view_projection, world_pos);

    // 2. Transform normal to World Space, then into View Space
    float3 world_normal = normalize(mul((float3x3)model.normal_matrix, input.normal));
    output.view_normal  = normalize(mul((float3x3)scene.view, world_normal));

    return output;
}

[shader("fragment")]
float4 FSMain(VertexOutput input) : SV_Target {
    float3 N = normalize(input.view_normal);

    // 3. Map View-Space Normal (-1..1) to MatCap UV space (0..1)
    // Note: N.y is inverted (-0.5) to align with Vulkan/glTF V-down texture coordinates
    float2 matcap_uv = N.xy * float2(0.5, -0.5) + 0.5;

    // 4. Sample MatCap texture
    float4 matcap_color = t_matcap.Sample(s_matcap, matcap_uv);

    // 5. Apply Material Tint & Intensity
    float3 final_rgb = matcap_color.rgb * material.base_color_factor.rgb * material.matcap_intensity;
    float  final_a   = matcap_color.a * material.base_color_factor.a;

    return float4(final_rgb, final_a);
}
