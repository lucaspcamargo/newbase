struct VSInput
{
    float2 pos_2d: POSITION;
    float4 color : COLOR;
    float2 uv    : TEXCOORD0;
};

struct VSOutput
{
    float4 pos   : SV_Position;
    float4 color : COLOR0;
    float2 uv    : TEXCOORD0;
};

cbuffer GlobalUniforms : register(b0, space1)
{
    column_major float4x4 u_Projection;
};

[shader("vertex")]
VSOutput main(VSInput input)
{
    VSOutput output;

    output.pos = mul(u_Projection, float4(input.pos_2d.x, input.pos_2d.y, 0.0, 1.0));
    output.color = input.color;
    output.uv = input.uv;

    return output;
}
