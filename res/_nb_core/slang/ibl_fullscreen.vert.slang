struct VSInput
{
    float2 pos_2d : POS_2D;
    float4 color  : COLOR;
    float2 uv     : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

[shader("vertex")]
VSOutput main(VSInput input)
{
    VSOutput output;
    output.position = float4(input.pos_2d, 0.0, 1.0);
    output.uv = input.uv;
    return output;
}
