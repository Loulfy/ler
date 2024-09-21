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
     float2(0, 0),
     float2(1, 0),
     float2(0, 1),
     float2(1, 1)
 };

 PSInput VSMain(uint vid : SV_VertexID)
 {
     PSInput result;

     result.uv = coords[vid];
     result.position = float4(positions[vid], 0.0, 1.0);

     return result;
 }

 //[[vk::binding(0), vk::combinedImageSampler]] Texture2D<float4> g_texture: register(t0);
 //[[vk::binding(0), vk::combinedImageSampler]] SamplerState g_sampler: register(s0);
 //[[vk::binding(0)]] ConstantBuffer<SkyBoxRenderResources> renderResource : register(b0);
 //[[vk::binding(0)]] Texture2D<float4> g_texture[]: register(t0);
 //[[vk::binding(1)]] SamplerState g_sampler: register(s0);

 //ConstantBuffer<SkyBoxRenderResources> renderResource : register(b3);

 [[vk::push_constant]]
 ConstantBuffer<SkyBoxRenderResources> renderResource;

 float4 PSMain(PSInput input) : SV_TARGET
 {
     //return float4(1,0,0,1);
     SamplerState g_sampler = SamplerDescriptorHeap[0];
     Texture2D<float4> g_texture = ResourceDescriptorHeap[renderResource.textureIndex];
     return g_texture.Sample(g_sampler, input.uv);
 }