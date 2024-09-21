struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

struct SkyBoxRenderResources
{
    uint textureIndex;
};

static const float2 positions[4] = {
    float2(-1, -1),
    float2(+1, -1),
    float2(-1, +1),
    float2(+1, +1)
};
static const float2 coords[4] = {
    float2(0, 1),
    float2(1, 1),
    float2(0, 0),
    float2(1, 0)
};

PSInput VSMain(uint vid : SV_VertexID)
{
    PSInput result;

    result.uv = coords[vid];
    result.position = float4(positions[vid], 0.0, 1.0);

    return result;
}

//Texture2D<float4> g_texture: register(t1);
//Texture2D<float4> p_texture: register(t2);
//SamplerState g_sampler: register(s0);

//ConstantBuffer<SkyBoxRenderResources> renderResource : register(b0);
uint textureIndex;

float4 PSMain(PSInput input) : SV_TARGET
{
    //return float4(1,0,0,1);
    SamplerState g_sampler = SamplerDescriptorHeap[0];
    Texture2D<float4> g_texture = ResourceDescriptorHeap[textureIndex];
    //float4 v = p_texture.Sample(g_sampler, input.uv);
    //float4 r = g_texture.Sample(g_sampler, input.uv);
    //return float4(v.x, v.y, v.z, r.w);

    //return g_texture.Sample(g_sampler, input.uv * renderResource.textureIndex);
    return g_texture.Sample(g_sampler, input.uv);
}