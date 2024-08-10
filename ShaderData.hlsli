struct VSInput
{
    float3 pos : POSITION;
    float2 uv : TEXCOORD;
};

typedef struct VSOutput
{
    float4 pos : SV_Position;
    float2 worldXY : POSITION;
    float2 uv : TEXCOORD;
} PSInput;

float2 RemapRangeVec2(
    const float2 value,
    const float2 oldMin,
    const float2 oldMax,
    const float2 newMin,
    const float2 newMax
)
{
    return newMin + (value - oldMin) / (oldMax - oldMin) * (newMax - newMin);
}