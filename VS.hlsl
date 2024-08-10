#include "ShaderData.hlsli"

VSOutput main(VSInput input)
{
    VSOutput output;
    output.pos = float4(input.pos, 1.f);
    output.worldXY = input.pos.xy;
    output.uv = input.uv;
    
    return output;
}