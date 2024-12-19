#include "common.hlsli"

struct Frustum
{
    float4 planes[6];
    float4 corners[8];
    uint num;
};

struct CullResources
{
    uint propIndex;
    uint meshIndex;
    uint drawIndex;
    uint countIndex;
    uint frustIndex;
};

VkPush ConstantBuffer<CullResources> cullResource : register(b0);

bool isAABBinFrustum(Frustum frustum, float3 min, float3 max)
{
    for (int i = 0; i < 6; i++) {
        int r = 0;
        r += ( dot( frustum.planes[i], float4(min.x, min.y, min.z, 1.0f) ) < 0.0 ) ? 1 : 0;
        r += ( dot( frustum.planes[i], float4(max.x, min.y, min.z, 1.0f) ) < 0.0 ) ? 1 : 0;
        r += ( dot( frustum.planes[i], float4(min.x, max.y, min.z, 1.0f) ) < 0.0 ) ? 1 : 0;
        r += ( dot( frustum.planes[i], float4(max.x, max.y, min.z, 1.0f) ) < 0.0 ) ? 1 : 0;
        r += ( dot( frustum.planes[i], float4(min.x, min.y, max.z, 1.0f) ) < 0.0 ) ? 1 : 0;
        r += ( dot( frustum.planes[i], float4(max.x, min.y, max.z, 1.0f) ) < 0.0 ) ? 1 : 0;
        r += ( dot( frustum.planes[i], float4(min.x, max.y, max.z, 1.0f) ) < 0.0 ) ? 1 : 0;
        r += ( dot( frustum.planes[i], float4(max.x, max.y, max.z, 1.0f) ) < 0.0 ) ? 1 : 0;
        if ( r == 8 ) return false;
    }

    int r = 0;
    r = 0; for ( int i = 0; i < 8; i++ ) r += ( (frustum.corners[i].x > max.x) ? 1 : 0 ); if ( r == 8 ) return false;
    r = 0; for ( int i = 0; i < 8; i++ ) r += ( (frustum.corners[i].x < min.x) ? 1 : 0 ); if ( r == 8 ) return false;
    r = 0; for ( int i = 0; i < 8; i++ ) r += ( (frustum.corners[i].y > max.y) ? 1 : 0 ); if ( r == 8 ) return false;
    r = 0; for ( int i = 0; i < 8; i++ ) r += ( (frustum.corners[i].y < min.y) ? 1 : 0 ); if ( r == 8 ) return false;
    r = 0; for ( int i = 0; i < 8; i++ ) r += ( (frustum.corners[i].z > max.z) ? 1 : 0 ); if ( r == 8 ) return false;
    r = 0; for ( int i = 0; i < 8; i++ ) r += ( (frustum.corners[i].z < min.z) ? 1 : 0 ); if ( r == 8 ) return false;

    return true;
}

groupshared uint drawOffset;

[numthreads(32, 1, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID)
{
    StructuredBuffer<Instance> props = ResourceDescriptorHeap[cullResource.propIndex];
    StructuredBuffer<Mesh> meshes = ResourceDescriptorHeap[cullResource.meshIndex];
    RWStructuredBuffer<Command> draws = ResourceDescriptorHeap[cullResource.drawIndex];
    RWBuffer<uint> drawCount = ResourceDescriptorHeap[cullResource.countIndex];
    ConstantBuffer<Frustum> frustum = ResourceDescriptorHeap[cullResource.frustIndex];

    uint instId = DTid.x;
    bool bDrawMesh = false;
    if(instId < frustum.num)
    {
        Instance obj = props[instId];
        Mesh mesh = meshes[obj.meshId];

        float4 mi = mul(obj.model, mesh.bbMin);
        float4 ma = mul(obj.model, mesh.bbMax);
        bDrawMesh = isAABBinFrustum(frustum, mi.xyz, ma.xyz);
        //bDrawMesh = true;

        uint drawMeshOffset = WavePrefixCountBits(bDrawMesh);
        uint drawMeshCount = WaveActiveCountBits(bDrawMesh);

        if(WaveIsFirstLane())
        {
            InterlockedAdd(drawCount[0], drawMeshCount, drawOffset);
        }

        GroupMemoryBarrierWithGroupSync();

        if(bDrawMesh)
        {
            Command drawCommand;
            drawCommand.baseInstance = 0;
            drawCommand.instanceCount = 1;
            drawCommand.firstIndex = mesh.firstIndex;
            drawCommand.countIndex = mesh.countIndex;
            drawCommand.baseVertex = mesh.firstVertex;
            drawCommand.instId = instId;

            uint drawCommandIndex = WaveReadLaneFirst(drawOffset);
            drawCommandIndex += drawMeshOffset;

            drawCommand.drawId = drawCommandIndex;
            draws[drawCommandIndex] = drawCommand;
        }
    }
}