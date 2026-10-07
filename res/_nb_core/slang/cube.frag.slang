struct LightData {
    float4 dirLightVector; // xyz = direction towards light source, w = unused
    float4 dirLightColor;  // rgb = color * intensity
    float4 ambientColor;   // rgb = base ambient color
};

// Set 3 (space3) - Slot 0: Fragment Uniforms
cbuffer Lighting : register(b0, space3) {
    LightData u_light;
};

struct FSInput {
    float3 worldNormal : NORMAL;
    float2 uv          : TEXCOORD0;
    float4 color       : COLOR0;
};

[shader("fragment")]
float4 main(FSInput input) : SV_Target0 {
    float3 N = normalize(input.worldNormal);
    float3 L = normalize(u_light.dirLightVector.xyz);

    // Directional Gouraud/Lambert Diffuse calculation
    float NdotL = max(0.0, dot(N, L));
    float3 diffuse = u_light.dirLightColor.rgb * NdotL;
    float3 ambient = u_light.ambientColor.rgb;

    float3 finalColor = (ambient + diffuse) * input.color.rgb;
    return float4(finalColor, input.color.a);
    //return float4(input.uv.r, input.uv.g, 0.0, input.color.a);
}
