//
// Created by Loulfy on 04/12/2023.
//

#include "rhi/d3d12.hpp"

#define GLFW_INCLUDE_NONE // Do not include any OpenGL/Vulkan headers
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_NATIVE_INCLUDE_NONE
#include <GLFW/glfw3native.h>

namespace ler::rhi::d3d12
{
SwapChainPtr Device::createSwapChain(GLFWwindow* window, bool vsync)
{
    auto swapChain = std::make_shared<SwapChain>(m_context);

    int width, height;
    glfwGetWindowSize(window, &width, &height);

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = ISwapChain::FrameCount;
    swapChainDesc.Width = width;
    swapChainDesc.Height = height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
    swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsSwapChainDesc = {};
    fsSwapChainDesc.Windowed = TRUE;

    ComPtr<IDXGISwapChain1> swapChain1;
    m_factory->CreateSwapChainForHwnd(m_context.queue, // Swap chain needs the queue so that it can force a flush on it.
                                      glfwGetWin32Window(window), &swapChainDesc, &fsSwapChainDesc, nullptr,
                                      &swapChain1);

    m_factory->MakeWindowAssociation(glfwGetWin32Window(window), DXGI_MWA_NO_ALT_ENTER);
    swapChain1.As(&swapChain->handle);

    swapChain->createNativeSync();
    swapChain->resize(width, height, vsync);

    return swapChain;
}

void SwapChain::createNativeSync()
{
    m_commandQueue = m_context.queue;

    for (UINT n = 0; n < SwapChain::FrameCount; ++n)
    {
        auto command = std::make_shared<Command>();

        // Command
        command->m_gpuHeap = m_context.descriptorPool[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV].get();
        m_context.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 IID_PPV_ARGS(&command->m_commandAllocator));
        m_context.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command->m_commandAllocator.Get(),
                                            nullptr, IID_PPV_ARGS(&command->m_commandList));

        command->m_commandList->Close();
        m_command[n] = command;
    }

    // Create synchronization objects and wait until assets have been uploaded to the GPU.
    {
        m_context.device->CreateFence(m_fenceValues[m_frameIndex], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
        m_fenceValues[m_frameIndex]++;

        // Create an event handle to use for frame synchronization.
        m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (m_fenceEvent == nullptr)
        {
            HRESULT_FROM_WIN32(GetLastError());
        }
    }
}

void SwapChain::resize(uint32_t width, uint32_t height, bool vsync)
{
    /*if (m_commandQueue && m_fence && m_fenceEvent)
    {
        // Schedule a Signal command in the GPU queue.
        const UINT64 fenceValue = m_fenceValues[m_frameIndex];
        if (SUCCEEDED(m_commandQueue->Signal(m_fence.Get(), fenceValue)))
        {
            // Wait until the Signal has been processed.
            if (SUCCEEDED(m_fence->SetEventOnCompletion(fenceValue, m_fenceEvent)))
            {
                std::ignore = WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);

                // Increment the fence value for the current frame.
                m_fenceValues[m_frameIndex]++;
            }
        }
    }*/

    for (UINT n = 0; n < FrameCount; n++)
    {
        m_fenceValues[n] = m_fenceValues[m_frameIndex];
        renderTargets[n].Reset();
    }

    std::array<IUnknown*, FrameCount> queues = {};
    queues.fill(m_commandQueue.Get());
    HRESULT hr = handle->ResizeBuffers(FrameCount, width, height, DXGI_FORMAT_R8G8B8A8_UNORM,
                                       DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING);
    if (FAILED(hr))
        log::error(getErrorMsg(hr));
    m_frameIndex = handle->GetCurrentBackBufferIndex();

    // Create frame resources.
    {
        // Create an RTV and a command allocator for each frame.
        for (UINT n = 0; n < SwapChain::FrameCount; ++n)
        {
            auto texture = std::make_shared<Texture>();

            // Texture
            handle->GetBuffer(n, IID_PPV_ARGS(&renderTargets[n]));
            std::wstring name(L"backBuffer");
            name += std::to_wstring(n);
            renderTargets[n]->SetName(name.c_str());

            texture->state = Common;
            texture->desc.Width = width;
            texture->desc.Height = height;
            texture->handle = renderTargets[n].Get();
            texture->rtvDescriptor = m_context.descriptorPool[D3D12_DESCRIPTOR_HEAP_TYPE_RTV]->allocate(1);
            m_context.device->CreateRenderTargetView(renderTargets[n].Get(), nullptr,
                                                     texture->rtvDescriptor.getCpuHandle());
            m_images[n] = texture;
        }
    }

    DXGI_SWAP_CHAIN_DESC1 desc;
    handle->GetDesc1(&desc);
    DxgiFormatMapping mapping = getDxgiFormatMapping(desc.Format);
    log::info("SwapChain: Images({}), Extent({}x{}), Format({})", SwapChain::FrameCount, width, height,
              to_string(mapping.abstractFormat));
}

Extent SwapChain::extent() const
{
    DXGI_SWAP_CHAIN_DESC1 desc;
    handle->GetDesc1(&desc);
    return { desc.Width, desc.Height };
}

Format SwapChain::format() const
{
    DXGI_SWAP_CHAIN_DESC1 desc;
    handle->GetDesc1(&desc);
    DxgiFormatMapping mapping = getDxgiFormatMapping(desc.Format);
    return mapping.abstractFormat;
}

uint32_t SwapChain::present(const RenderPass& renderPass)
{
    m_command[m_frameIndex]->reset();

    renderPass(m_images[m_frameIndex], m_command[m_frameIndex]);

    auto* command = checked_cast<Command*>(m_command[m_frameIndex].get());
    // Execute the command list.
    command->m_commandList->Close();
    ID3D12CommandList* ppCommandLists[] = { command->m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, ppCommandLists);

    handle->Present(1, 0);

    // Schedule a Signal command in the queue.
    const UINT64 currentFenceValue = m_fenceValues[m_frameIndex];
    m_commandQueue->Signal(m_fence.Get(), currentFenceValue);

    // Update the frame index.
    m_frameIndex = handle->GetCurrentBackBufferIndex();

    // If the next frame is not ready to be rendered yet, wait until it is ready.
    if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex])
    {
        m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent);
        WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
    }

    // Set the fence value for the next frame.
    m_fenceValues[m_frameIndex] = currentFenceValue + 1;

    return 0;
}
} // namespace ler::rhi::d3d12