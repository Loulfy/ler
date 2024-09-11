#pragma pack_matrix(column_major)

struct Instance
{
    float4x4 model;
    uint meshId;
    uint skinId;
    float2 pad;
};

struct Command
{
    uint countIndex;
    uint instanceCount;
    uint firstIndex;
    int baseVertex;
    uint baseInstance;
    uint drawId;
};

struct Material
{
    uint d;
    uint3 pad;
};

struct UBO
{
    float4x4 proj;
    float4x4 view;
};

struct VSInput
{
    [[vk::location(0)]] float3 pos : POSITION0;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float3 tangent : TANGENT;
    [[vk::location(3)]] float3 uv : TEXCOORD0;
    uint id : SV_InstanceID;
};

struct PSInput
{
    float4 pos : SV_POSITION;
    [[vk::location(0)]] float3 uv : TEXCOORD0;
    [[vk::location(1)]] uint matId : MATERIAL;
};

//[[vk::binding(0)]] cbuffer push : register(b0) { UBO pc; }
[[vk::binding(0)]] ConstantBuffer<UBO> pc : register(b0);
[[vk::binding(1)]] StructuredBuffer<Instance> props : register(t0);
[[vk::binding(4)]] StructuredBuffer<Command> draws : register(t1);

PSInput VSMain(VSInput input)
{
    PSInput result;

    Instance inst = props[draws[input.id].drawId];

    float4 tmpPos = float4(input.pos, 1.0);
    result.pos = mul(pc.proj, mul(pc.view, mul(inst.model, tmpPos)));
    result.uv = input.uv;
    result.matId = inst.skinId;

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return float4(1.0, 0.0, 0.0, 1.0);
}