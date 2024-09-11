#pragma pack_matrix(column_major)

struct VSInput
{
    float3 pos : POSITION;
    float3 uv : TEXCOORD0;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    uint id : SV_InstanceID;
};

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
    uint baseVertex;
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

struct PSInput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    uint matId : MATERIAL;
};

StructuredBuffer<Instance> props : register(t1);
StructuredBuffer<Command> draws : register(t3);

cbuffer push : register(b0) { UBO pc; }

PSInput VSMain(VSInput input)
{
    PSInput result;

    uint drawIndex = draws[15].drawId;
    Instance inst = props[drawIndex];

    float4 tmpPos = float4(input.pos, 1.0);
    result.pos = mul(pc.proj, mul(pc.view, mul(inst.model, tmpPos)));
    result.uv = input.uv.xy;
    result.matId = inst.skinId;

    return result;
}

SamplerState samplerColor : register(s4);
Texture2D<float4> textures[] : register(t4, space10);
StructuredBuffer<Material> mats : register(t2);

float4 PSMain(PSInput input) : SV_TARGET
{
    Material m = mats[input.matId];
    float4 color = textures[NonUniformResourceIndex(m.d)].Sample(samplerColor, input.uv);
    return float4(color.xyz, 1.0);
    //return float4(1.0, 0.0, 0.0, 1.0);
}