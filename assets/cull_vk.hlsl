#pragma pack_matrix(column_major)

struct Instance
{
    float4x4 model;
    uint meshId;
    uint skinId;
    float2 pad;
};

struct Mesh
{
    float4 bbMin;
    float4 bbMax;
    uint countIndex;
    uint firstIndex;
    int firstVertex;
    uint countVertex;
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

[[vk::binding(0)]] StructuredBuffer<Instance> props : register(t0);
[[vk::binding(1)]] StructuredBuffer<Mesh> meshes : register(t1);
[[vk::binding(2)]] RWStructuredBuffer<Command> draws : register(u0);
[[vk::binding(3)]] RWStructuredBuffer<uint> drawCount : register(u1);

[numthreads(64, 1, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID)
{
    uint drawId = DTid.x;
    Instance inst = props[drawId];

    Mesh mesh = meshes[inst.meshId];

    Command drawCommand;
    drawCommand.baseInstance = 0;
    drawCommand.instanceCount = 1;
    drawCommand.firstIndex = mesh.firstIndex;
    drawCommand.countIndex = mesh.countIndex;
    drawCommand.baseVertex = mesh.firstVertex;
    drawCommand.drawId = drawId;

    draws[drawId] = drawCommand;
    InterlockedAdd(drawCount[0], 1);
}