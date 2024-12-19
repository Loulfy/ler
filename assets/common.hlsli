#pragma pack_matrix(column_major)

#ifdef __spirv__
#define VkPush [[vk::push_constant]]
#else
#define VkPush
#endif

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
    uint firstVertex;
    uint countVertex;
};

#ifdef __spirv__
struct Command
{
    uint countIndex;
    uint instanceCount;
    uint firstIndex;
    uint baseVertex;
    uint baseInstance;
    uint instId;
    uint drawId;
};
#else
struct Command
{
    uint drawId;
    uint countIndex;
    uint instanceCount;
    uint firstIndex;
    uint baseVertex;
    uint baseInstance;
    uint instId;
};
#endif