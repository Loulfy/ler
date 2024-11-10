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

struct UBO
{
    float4x4 proj;
    float4x4 view;
};

struct PSInput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

[[vk::push_constant]]
ConstantBuffer<UBO> pc;

PSInput VSMain(VSInput input)
{
    PSInput result;

    StructuredBuffer<Instance> props = ResourceDescriptorHeap[0];
    Instance inst = props[input.id];

    float4 tmpPos = float4(input.pos, 1.0);
    result.pos = mul(pc.proj, mul(pc.view, mul(inst.model, tmpPos)));
    result.uv = input.uv.xy;

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return float4(1.0, 0.0, 0.0, 1.0);
}