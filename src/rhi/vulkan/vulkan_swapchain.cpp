//
// Created by loulfy on 04/12/2023.
//

#include "rhi/vulkan.hpp"

#define GLFW_INCLUDE_NONE // Do not include any OpenGL/Vulkan headers
#include <GLFW/glfw3.h>

namespace ler::rhi::vulkan
{
    vk::PresentModeKHR
    SwapChain::chooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& availablePresentModes, bool vSync)
    {
        for (const auto& availablePresentMode: availablePresentModes)
        {
            if (availablePresentMode == vk::PresentModeKHR::eFifo && vSync)
                return availablePresentMode;
            if (availablePresentMode == vk::PresentModeKHR::eMailbox && !vSync)
                return availablePresentMode;
        }
        return vk::PresentModeKHR::eImmediate;
    }

    vk::SurfaceFormatKHR SwapChain::chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats)
    {
        if (availableFormats.size() == 1 && availableFormats[0].format == vk::Format::eUndefined)
        {
            return {vk::Format::eB8G8R8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear};
        }

        for (const auto& format: availableFormats)
        {
            if ((format.format == vk::Format::eR8G8B8A8Unorm || format.format == vk::Format::eB8G8R8A8Unorm) && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
                return format;
        }

        log::exit("found no suitable surface format");
        return {};
    }

    vk::Extent2D
    SwapChain::chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities, uint32_t width, uint32_t height)
    {
        if (capabilities.currentExtent.width == UINT32_MAX)
        {
            vk::Extent2D extent(width, height);
            vk::Extent2D minExtent = capabilities.minImageExtent;
            vk::Extent2D maxExtent = capabilities.maxImageExtent;
            extent.width = std::clamp(extent.width, minExtent.width, maxExtent.width);
            extent.height = std::clamp(extent.height, minExtent.height, maxExtent.height);
            return extent;
        }
        else
        {
            return capabilities.currentExtent;
        }
    }

    SwapChainPtr Device::createSwapChain(GLFWwindow* window)
    {
        auto swapChain = std::make_shared<SwapChain>(m_context);

        int width, height;
        glfwGetWindowSize(window, &width, &height);

        // Init surface
        VkSurfaceKHR glfwSurface;
        auto res = glfwCreateWindowSurface(m_context.instance, window, nullptr, &glfwSurface);
        if (res != VK_SUCCESS)
            log::exit("Failed to create window surface");

        swapChain->surface = vk::UniqueSurfaceKHR(vk::SurfaceKHR(glfwSurface), { m_context.instance });
        vk::SurfaceKHR surface = swapChain->surface.get();

        // Create swapChain Info
        using vkIU = vk::ImageUsageFlagBits;
        vk::SwapchainCreateInfoKHR createInfo;
        createInfo.setSurface(surface);
        createInfo.setImageArrayLayers(1);
        createInfo.setImageUsage(vkIU::eColorAttachment | vkIU::eTransferDst);
        createInfo.setImageSharingMode(vk::SharingMode::eExclusive);
        createInfo.setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque);
        createInfo.setClipped(true);

        swapChain->device = this;
        swapChain->createInfo = createInfo;
        swapChain->createNativeSync();
        swapChain->resize(width, height);

        return swapChain;
    }

    void SwapChain::createNativeSync()
    {
        m_queue = m_context.device.getQueue(m_context.graphicsQueueFamily, 0);

        for (size_t i = 0; i < FrameCount; ++i)
        {
            m_command[i] = device->getGraphicsQueue()->getOrCreateCommandBuffer();
            const auto* command = checked_cast<Command*>(m_command[i].get());
            m_rawCommand[i] = command->cmdBuf;
            m_acquireSemaphore[i] = m_context.device.createSemaphoreUnique({});
            m_presentSemaphore[i] = m_context.device.createSemaphoreUnique({});
            m_fences[i] = m_context.device.createFenceUnique({vk::FenceCreateFlagBits::eSignaled});
        }
    }

    void SwapChain::resize(uint32_t width, uint32_t height)
    {
        // we can not create minimized swap chain
        if(width == 0 || height == 0)
            return;

        // we can not destroy swap chain with pending frames
        m_context.device.waitIdle();

        // Setup viewports, vSync
        std::vector<vk::SurfaceFormatKHR> surfaceFormats = m_context.physicalDevice.getSurfaceFormatsKHR(surface.get());
        vk::SurfaceCapabilitiesKHR surfaceCapabilities = m_context.physicalDevice.getSurfaceCapabilitiesKHR(surface.get());
        std::vector<vk::PresentModeKHR> surfacePresentModes = m_context.physicalDevice.getSurfacePresentModesKHR(surface.get());

        const vk::Extent2D extent = SwapChain::chooseSwapExtent(surfaceCapabilities, width, height);
        vk::SurfaceFormatKHR surfaceFormat = SwapChain::chooseSwapSurfaceFormat(surfaceFormats);
        vk::PresentModeKHR presentMode = SwapChain::chooseSwapPresentMode(surfacePresentModes, false);

        uint32_t backBufferCount = std::clamp(surfaceCapabilities.minImageCount, 1U, FrameCount);

        // Update & Create SwapChain
        createInfo.setPreTransform(surfaceCapabilities.currentTransform);
        createInfo.setImageExtent({width, height});
        createInfo.setImageColorSpace(surfaceFormat.colorSpace);
        createInfo.setMinImageCount(backBufferCount);
        createInfo.setImageFormat(surfaceFormat.format);
        createInfo.setPresentMode(presentMode);
        m_handle = m_context.device.createSwapchainKHRUnique(createInfo);

        auto swapChainImages = m_context.device.getSwapchainImagesKHR(m_handle.get());
        for(size_t i = 0; i < swapChainImages.size(); ++i)
        {
            vk::Image& image = swapChainImages[i];
            auto texture = std::make_shared<Texture>(m_context);

            texture->info = vk::ImageCreateInfo();
            texture->info.setImageType(vk::ImageType::e2D);
            texture->info.setExtent(vk::Extent3D(extent.width, extent.height, 1));
            texture->info.setSamples(vk::SampleCountFlagBits::e1);
            texture->info.setFormat(createInfo.imageFormat);
            texture->info.setArrayLayers(1);

            texture->handle = image;
            texture->allocation = nullptr;
            texture->setName("backBuffer" + std::to_string(i));

            m_images.emplace_back(texture);
        }

        log::info("SwapChain: Images({}), Extent({}x{}), Format({}), Present({})",
            backBufferCount,
            extent.width, extent.height,
            vk::to_string(surfaceFormat.format),
            vk::to_string(presentMode)
        );
    }

    Extent SwapChain::extent() const
    {
        vk::Extent2D ext = createInfo.imageExtent;
        return {ext.width, ext.height};
    }

    Format SwapChain::format() const
    {
        return Device::reverseFormat(createInfo.imageFormat);
    }

    uint32_t SwapChain::present(const RenderPass& renderPass)
    {
        // Wait busy command
        vk::Result result = m_context.device.waitForFences(1, &m_fences[currentFrame].get(), VK_TRUE, UINT64_MAX);
        assert(result == vk::Result::eSuccess);
        result = m_context.device.resetFences(1, &m_fences[currentFrame].get());
        assert(result == vk::Result::eSuccess);

        // Acquire next frame
        result = m_context.device.acquireNextImageKHR(m_handle.get(), UINT64_MAX, m_acquireSemaphore[currentFrame].get(), vk::Fence(), &swapChainIndex);
        assert(result == vk::Result::eSuccess || result == vk::Result::eSuboptimalKHR);

        // Begin command
        m_command[currentFrame]->reset();

        // Record pass
        renderPass(m_images[swapChainIndex], m_command[currentFrame]);
        m_rawCommand[currentFrame].end();

        // Submit
        vk::SubmitInfo submitInfo;
        static constexpr vk::PipelineStageFlags waitMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        submitInfo.setWaitDstStageMask(waitMask);
        submitInfo.setWaitSemaphoreCount(1);
        submitInfo.setPWaitSemaphores(&m_acquireSemaphore[currentFrame].get());
        submitInfo.setSignalSemaphoreCount(1);
        submitInfo.setPSignalSemaphores(&m_presentSemaphore[currentFrame].get());
        submitInfo.setCommandBuffers(m_rawCommand[currentFrame]);
        m_queue.submit(submitInfo, m_fences[currentFrame].get());

        // Present
        vk::PresentInfoKHR presentInfo;
        presentInfo.setWaitSemaphoreCount(1);
        presentInfo.setPWaitSemaphores(&m_presentSemaphore[currentFrame].get());
        presentInfo.setSwapchainCount(1);
        presentInfo.setPSwapchains(&m_handle.get());
        presentInfo.setPImageIndices(&swapChainIndex);

        result = m_queue.presentKHR(&presentInfo);
        assert(result == vk::Result::eSuccess || result == vk::Result::eSuboptimalKHR);

        currentFrame = (currentFrame + 1) % ISwapChain::FrameCount;
        return currentFrame;
    }
}