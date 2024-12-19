#include "common.hlsli"

struct VSInput
{
    float3 pos : POSITION;
    //float3 uv : TEXCOORD0;
    //float3 normal : NORMAL;
    //float3 tangent : TANGENT;
#ifdef __spirv__
    [[vk::builtin("DrawIndex")]]
    uint id : SVDrawIndex;
#endif
};

struct UBO
{
    float4x4 proj;
    float4x4 view;
};

struct DrawArg
{
    uint drawId;
};

struct PSInput
{
    float4 pos : SV_POSITION;
    //float2 uv : TEXCOORD0;
};

VkPush ConstantBuffer<UBO> pc : register(b0);
#ifndef __spirv__
ConstantBuffer<DrawArg> gl : register(b1);
#endif

PSInput VSMain(VSInput input)
{
    PSInput result;

    StructuredBuffer<Instance> props = ResourceDescriptorHeap[0];
    StructuredBuffer<Command> draws = ResourceDescriptorHeap[2];

#ifdef __spirv__
    Command cmd = draws[input.id];
#else
    Command cmd = draws[gl.drawId];
#endif

    Instance inst = props[cmd.instId];

    float4 tmpPos = float4(input.pos, 1.0);
    result.pos = mul(pc.proj, mul(pc.view, mul(inst.model, tmpPos)));
    //result.uv = input.uv.xy;

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return float4(1.0, 0.0, 0.0, 1.0);
}