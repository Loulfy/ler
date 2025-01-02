//
// Created by Loulfy on 22/11/2024.
//

#include "rhi/metal.hpp"

#define GLFW_INCLUDE_NONE // Do not include any OpenGL/Vulkan headers
#include <GLFW/glfw3.h>

void addLayerToWindow(GLFWwindow* window, CA::MetalLayer* layer);

namespace ler::rhi::metal
{
SwapChainPtr Device::createSwapChain(GLFWwindow* window, bool vsync)
{
    auto swapChain = std::make_shared<SwapChain>(m_context);

    int width, height;
    glfwGetWindowSize(window, &width, &height);

    swapChain->layer = NS::TransferPtr(CA::MetalLayer::layer());
    swapChain->layer->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    swapChain->layer->setDevice(m_context.device);
    swapChain->layer->setFramebufferOnly(true);
    addLayerToWindow(window, swapChain->layer.get());
    swapChain->createNativeSync();

    return swapChain;
}

void SwapChain::createNativeSync()
{
    for (uint32_t n = 0; n < SwapChain::FrameCount; ++n)
    {
        auto command = std::make_shared<Command>(m_context);
        m_command[n] = command;
    }

    m_fence = NS::TransferPtr(m_context.device->newSharedEvent());
    m_fenceValues[m_frameIndex]++;

    m_imageAcquireSemaphore = dispatch_semaphore_create(SwapChain::FrameCount);
}

void SwapChain::resize(uint32_t width, uint32_t height, bool vsync)
{
    layer->setDrawableSize(CGSizeMake(width, height));
}

Extent SwapChain::extent() const
{
    const CGSize size = layer->drawableSize();
    return {static_cast<uint32_t>(size.width), static_cast<uint32_t>(size.height)};
}

uint32_t SwapChain::present(const RenderPass& renderPass)
{
    /*m_command[m_frameIndex]->reset();

    renderPass(m_images[m_frameIndex], m_command[m_frameIndex]);

    auto* command = checked_cast<Command*>(m_command[m_frameIndex].get());

    command->cmdBuf->presentDrawable(layer->nextDrawable());

    // Schedule a Signal command in the queue.
    const uint64_t currentFenceValue = m_fenceValues[m_frameIndex];
    command->cmdBuf->encodeSignalEvent(m_fence.get(), currentFenceValue);

    // Execute the command list.
    command->cmdBuf->commit();
    command->cmdBuf->waitUntilCompleted();

    // Update the frame index.
    m_frameIndex = (m_frameIndex + 1) % SwapChain::FrameCount;
    //m_frameIndex = handle->GetCurrentBackBufferIndex();

    // If the next frame is not ready to be rendered yet, wait until it is ready.
    if (m_fence->signaledValue() < m_fenceValues[m_frameIndex])
        m_fence->waitUntilSignaledValue(m_fenceValues[m_frameIndex], 0);

    // Set the fence value for the next frame.
    m_fenceValues[m_frameIndex] = currentFenceValue + 1;*/

    dispatch_semaphore_wait(m_imageAcquireSemaphore, DISPATCH_TIME_FOREVER);

    m_command[m_frameIndex]->reset();

    NS::SharedPtr<CA::MetalDrawable> drawable = NS::TransferPtr(layer->nextDrawable());
    auto backBuffer = std::make_shared<Texture>(m_context);
    backBuffer->handle = drawable->texture();

    TexturePtr test = backBuffer;
    renderPass(test, m_command[m_frameIndex]);

    /*MTL::RenderPassDescriptor* renderPassDescriptor = MTL::RenderPassDescriptor::alloc()->init();
    MTL::RenderPassColorAttachmentDescriptor* cd = renderPassDescriptor->colorAttachments()->object(0);
    cd->setTexture(drawable->texture());
    cd->setLoadAction(MTL::LoadActionClear);
    cd->setClearColor(MTL::ClearColor(41.0f/255.0f, 42.0f/255.0f, 48.0f/255.0f, 1.0));
    cd->setStoreAction(MTL::StoreActionStore);*/

    auto* command = checked_cast<Command*>(m_command[m_frameIndex].get());
    command->cmdBuf->addCompletedHandler(^(MTL::CommandBuffer*){
        dispatch_semaphore_signal(m_imageAcquireSemaphore);
    });

    //MTL::RenderCommandEncoder* renderCommandEncoder = command->cmdBuf->renderCommandEncoder(renderPassDescriptor);
    //renderCommandEncoder->endEncoding();

    // Present
    command->cmdBuf->presentDrawable(drawable.get());

    // Execute the command list.
    command->cmdBuf->commit();

    // Update the frame index.
    m_frameIndex = (m_frameIndex + 1) % SwapChain::FrameCount;

    return 0;
}
} // namespace ler::rhi::metal