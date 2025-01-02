
#include "imgui.h"
#include "imgui_impl_metal.h"
#import <Metal/Metal.hpp>

#pragma mark - Dear ImGui Metal C++ Backend API

bool ImGui_ImplMetal_Init(MTL::Device* device)
{
    return ImGui_ImplMetal_Init((__bridge id<MTLDevice>)(device));
}

void ImGui_ImplMetal_NewFrame(MTL::RenderPassDescriptor* renderPassDescriptor)
{
    ImGui_ImplMetal_NewFrame((__bridge MTLRenderPassDescriptor*)(renderPassDescriptor));
}

void ImGui_ImplMetal_RenderDrawData(ImDrawData* draw_data,
                                    MTL::CommandBuffer* commandBuffer,
                                    MTL::RenderCommandEncoder* commandEncoder)
{
    ImGui_ImplMetal_RenderDrawData(draw_data,
                                   (__bridge id<MTLCommandBuffer>)(commandBuffer),
                                   (__bridge id<MTLRenderCommandEncoder>)(commandEncoder));

}

bool ImGui_ImplMetal_CreateFontsTexture(MTL::Device* device)
{
    return ImGui_ImplMetal_CreateFontsTexture((__bridge id<MTLDevice>)(device));
}

bool ImGui_ImplMetal_CreateDeviceObjects(MTL::Device* device)
{
    return ImGui_ImplMetal_CreateDeviceObjects((__bridge id<MTLDevice>)(device));
}