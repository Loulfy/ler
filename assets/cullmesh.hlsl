#include "common.hlsli"

#define FLT_MAX  3.402823466e+38f
#define FLT_MIN -3.402823466e+38f

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

void transformBoundingBox(float4x4 t, inout float3 bmin, inout float3 bmax)
{
    float3 pts[8] = {
        float3(bmin.x, bmin.y, bmin.z),
        float3(bmin.x, bmax.y, bmin.z),
        float3(bmin.x, bmin.y, bmax.z),
        float3(bmin.x, bmax.y, bmax.z),
        float3(bmax.x, bmin.y, bmin.z),
        float3(bmax.x, bmax.y, bmin.z),
        float3(bmax.x, bmin.y, bmax.z),
        float3(bmax.x, bmax.y, bmax.z)
    };

    // Transformation des 8 points
    for (int i = 0; i < 8; i++)
    {
        float4 p = mul(t, float4(pts[i], 1.0f));
        pts[i] = p.xyz / p.w; // Diviser par w si la transformation l'exige
    }

    // Réinitialisation des bornes
    bmin = float3(FLT_MAX, FLT_MAX, FLT_MAX);
    bmax = float3(FLT_MIN, FLT_MIN, FLT_MIN);

    // Recalcul du min/max
    for (int i = 0; i < 8; i++)
    {
        bmin = min(bmin, pts[i]);
        bmax = max(bmax, pts[i]);
    }
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

        //float4 mi = mul(obj.model, mesh.bbMin);
        //float4 ma = mul(obj.model, mesh.bbMax);
        float3 mi = mesh.bbMin.xyz;
        float3 ma = mesh.bbMax.xyz;
        transformBoundingBox(obj.model, mi, ma);
        bDrawMesh = isAABBinFrustum(frustum, mi, ma);
        //bDrawMesh = true;

        uint drawMeshOffset = WavePrefixCountBits(bDrawMesh);
        uint drawMeshCount = WaveActiveCountBits(bDrawMesh);

        if(WaveIsFirstLane())
        {
            InterlockedAdd(drawCount[1], drawMeshCount, drawOffset);
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