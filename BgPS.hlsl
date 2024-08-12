#include "ShaderData.hlsli"

Texture2D tex : register(t0);
SamplerState samplerState : register(s0);

cbuffer player1PosBuffer : register(b0)
{
    float2 player1Pos;
}

cbuffer player1PosBuffer : register(b1)
{
    float2 player2Pos;
}

float4 main(PSInput input) : SV_TARGET
{
    const float RADIUS = 0.03f;
    const float RADIUS_SQ = RADIUS * RADIUS;
    
    const float2 pos1 = clamp(player1Pos, -1.f, 1.f);
    const float2 pos2 = clamp(player2Pos, -1.f, 1.f);
    
    const float2 p1Diff = pos1 - input.worldXY;
    const float2 p2Diff = pos2 - input.worldXY;
    
    const float p1DistSq = dot(p1Diff, p1Diff);
    const float p2DistSq = dot(p2Diff, p2Diff);
    
    const bool isInCircle1 = p1DistSq < RADIUS_SQ;
    const bool isInCircle2 = p2DistSq < RADIUS_SQ;
    if (isInCircle1 || isInCircle2)
    {
        return isInCircle1 && isInCircle2
            ? float4(0.f, 0.f, 1.f, 1.f)
            : float4(0.f, 0.f, 0.f, 1.f);
    }
    else
    {
        const float2 camPos = RemapRangeVec2(
            pos1,
            float2(-1.f, -1.f),
            float2(1.f, 1.f),
            float2(0.f, 0.f),
            float2(8.f, 10.f)
        );
    
        const float2 halfLength = float2(0.5f, 0.5f);
    
        const float2 texCoord = RemapRangeVec2(
            input.uv,
            float2(0.f, 0.f),
            float2(1.f, 1.f),
            float2(1 / 8.f, 1 / 10.f) * (camPos - halfLength),
            float2(1 / 8.f, 1 / 10.f) * (camPos + halfLength)
        );
    
        return tex.Sample(samplerState, texCoord);
    }
}
