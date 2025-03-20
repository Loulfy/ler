#include "common.hlsli"

struct VSInput
{
    float3 pos : POSITION;
    float3 uv : TEXCOORD0;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
#ifdef __spirv__
    [[vk::builtin("DrawIndex")]]
    uint id : SVDrawIndex;
#endif
};

struct UBO
{
    float4x4 proj;
    float4x4 view;
    uint drawsIndex;
    uint instIndex;
    uint matIndex;
    uint padb;
};

struct DrawArg
{
    uint drawId;
};

struct PSInput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    uint instId : POUET;
};

VkPush ConstantBuffer<UBO> pc : register(b0);
#ifndef __spirv__
ConstantBuffer<DrawArg> gl : register(b1);
#endif

PSInput VSMain(VSInput input)
{
    PSInput result;

    StructuredBuffer<Instance> props = ResourceDescriptorHeap[pc.drawsIndex];
    StructuredBuffer<Command> draws = ResourceDescriptorHeap[pc.instIndex];

#ifdef __spirv__
    Command cmd = draws[input.id];
#else
    Command cmd = draws[gl.drawId];
#endif

    Instance inst = props[cmd.instId];

    float4 tmpPos = float4(input.pos, 1.0);
    result.pos = mul(pc.proj, mul(pc.view, mul(inst.model, tmpPos)));
    result.uv = input.uv.xy;
    result.instId = cmd.instId;

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    StructuredBuffer<Instance> props = ResourceDescriptorHeap[pc.drawsIndex];
    StructuredBuffer<Material> mats = ResourceDescriptorHeap[pc.matIndex];
    Instance inst = props[input.instId];
    Material m = mats[inst.skinId];

    SamplerState g_sampler = SamplerDescriptorHeap[0];
    Texture2D<float4> texture = ResourceDescriptorHeap[m.tex.y];
    float4 baseColor = texture.Sample(g_sampler, input.uv) * float4(m.baseColor, 1.f);

    /*if(m.alphaMode == 0)
        baseColor.a = 1.f;
    else if(m.alphaMode == 2)
        clip(baseColor.a - 0.01f);
    if(m.alphaMode == 1)
    {
        if(baseColor.a < m.alphaCutOff)
            discard;
    }*/

    if(baseColor.a < m.alphaCutOff)
        discard;

    return baseColor;

/*if(input.instId == 0)
    return float4(1.0, 0.0, 0.0, 1.0);
if(input.instId == 1)
    return float4(0.0, 1.0, 0.0, 1.0);
if(input.instId == 2)
    return float4(0.0, 0.0, 1.0, 1.0);
else
    return float4(1.0, 0.0, 1.0, 1.0);*/
}