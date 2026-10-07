struct FSInput
{
    float4 color : COLOR0;
    float2 uv    : TEXCOORD0;
};

// Combined texture-sampler bound at slot 0 of set 2
[[vk::binding(0,2)]]
Sampler2D u_Texture : register(t0,space2);

[shader("fragment")]
float4 main(FSInput input) : SV_Target
{
    float4 texColor = u_Texture.Sample(input.uv);
    return texColor * input.color;
}
