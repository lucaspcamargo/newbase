struct FSInput
{
    float4 color : COLOR0;
    float2 uv    : TEXCOORD0; // Unused
};


[shader("fragment")]
float4 main(FSInput input) : SV_Target
{
    return input.color;
}
