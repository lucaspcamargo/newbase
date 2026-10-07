struct TransformData {
    column_major float4x4 mvp;   // Model-View-Projection Matrix
    column_major float4x4 model; // World Matrix (for normal transformation)
};

// Set 1 (space1) - Slot 0: Vertex Uniforms
cbuffer Transforms : register(b0, space1) {
    TransformData u_transform;
};

struct VSInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal   : NORMAL;
    [[vk::location(2)]] float2 uv       : TEXCOORD0;
    [[vk::location(3)]] float4 color    : COLOR0;
};

struct VSOutput {
    float4 position    : SV_Position;
    float3 worldNormal : NORMAL;
    float2 uv          : TEXCOORD0;
    float4 color       : COLOR0;
};

[shader("vertex")]
VSOutput main(VSInput input) {
    VSOutput output;

    // Transform vertex position to Clip Space
    output.position = mul(u_transform.mvp, float4(input.position, 1.0));

    // Transform normal to World Space
    output.worldNormal = mul((float3x3)u_transform.model, input.normal);

    output.uv = input.uv;
    output.color = input.color;

    return output;
}
