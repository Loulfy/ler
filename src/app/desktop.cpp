//
// Created by loulfy on 04/12/2023.
//

#include "desktop.hpp"
#ifdef _WIN32
#include "rhi/d3d12.hpp"
#endif
#include "rhi/vulkan.hpp"
#include "img/loader.hpp"

#define GLFW_INCLUDE_NONE // Do not include any OpenGL/Vulkan headers
#include <GLFW/glfw3.h>
#include <imgui_impl_glfw.h>

namespace ler::app
{
    DesktopApp::DesktopApp(AppConfig cfg)
    {
        log::setup(log::level::debug);
        log::info("LER DesktopApp Init");
        log::info("CPU: {}, {} Threads", sys::getCpuName(), std::thread::hardware_concurrency());
        log::info("RAM: {} Go", sys::getRamCapacity()/1024);

        if (!glfwInit())
            log::exit("Failed to init glfw");

        // Init window
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, cfg.resizable);
        m_window = glfwCreateWindow(static_cast<int>(cfg.width), static_cast<int>(cfg.height), "LER", nullptr, nullptr);
        glfwSetWindowUserPointer(m_window, this);
        glfwSetKeyCallback(m_window, glfw_key_callback);
        glfwSetScrollCallback(m_window, glfw_scroll_callback);
        glfwSetMouseButtonCallback(m_window, glfw_mouse_callback);
        glfwSetWindowSizeCallback(m_window, glfw_size_callback);

        // Because it's shiny
        updateWindowIcon(sys::ASSETS_DIR / "wrench.png");

        uint32_t count;
        const char** extensions = glfwGetRequiredInstanceExtensions(&count);

        rhi::DeviceConfig devConfig;
        devConfig.debug = cfg.debug;
        devConfig.hostBuffer = !cfg.debug;
        devConfig.extensions.assign(extensions, extensions + count);

        if(cfg.api == rhi::GraphicsAPI::VULKAN)
            m_device = rhi::vulkan::CreateDevice(devConfig);

        if(cfg.api == rhi::GraphicsAPI::D3D12)
#ifdef _WIN32
            m_device = rhi::d3d12::CreateDevice(devConfig);
#else
            log::exit("D3D12 Not compatible");
#endif

        m_device->shaderAutoCompile();
        m_swapChain = m_device->createSwapChain(m_window, cfg.vsync);
        m_camera = std::make_shared<cam::Camera>();

        if(cfg.api == rhi::GraphicsAPI::VULKAN)
            m_camera->setFlipY(true);

        int w, h;
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        glfwGetFramebufferSize(m_window, &w, &h);
        io.DisplaySize = ImVec2(static_cast<float>(w), static_cast<float>(h));
        // io.ConfigFlags = ImGuiConfigFlags_DockingEnable;
        ImGui::StyleColorsDark();
    }

    void DesktopApp::updateWindowIcon(const fs::path& path) const
    {
        GLFWimage icon;
        const img::ImagePtr img = img::ImageLoader::load(path);
        icon.width = static_cast<int>(img->extent().width);
        icon.height = static_cast<int>(img->extent().width);
        icon.pixels = img->data();
        glfwSetWindowIcon(m_window, 1, &icon);
    }

    void DesktopApp::notifyResize()
    {
        for (const auto& pass : m_renderPasses)
            pass->resize(m_device, m_swapChain->extent());
    }

    void DesktopApp::resize(int width, int height)
    {
        m_swapChain->resize(width, height, true);
        notifyResize();
    }

    void DesktopApp::run()
    {
        if(m_device->getGraphicsAPI() == rhi::GraphicsAPI::VULKAN)
        {
            ImGui_ImplGlfw_InitForVulkan(m_window, true);
            addPass<rhi::vulkan::ImGuiPass>();
        }
    #ifdef _WIN32
        else
        {
            ImGui_ImplGlfw_InitForOther(m_window, true);
            addPass<rhi::d3d12::ImGuiPass>();
        }
    #endif

        double x, y;
        for (const auto& pass : m_renderPasses)
            pass->create(m_device, m_swapChain);

        notifyResize();

        while (!glfwWindowShouldClose(m_window))
        {
            glfwPollEvents();

            glfwGetCursorPos(m_window, &x, &y);
            m_camera->handleMouseMove(x, y);
            m_camera->updateViewMatrix();

            render::RenderParams params;
            params.proj = m_camera->getProjMatrix();
            params.view = m_camera->getViewMatrix();
            params.meshes = &m_meshBuffers;

            for (const auto& pass : m_renderPasses)
                pass->begin();

            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            m_swapChain->present([&](rhi::TexturePtr& backBuffer, rhi::CommandPtr& command){
                command->addImageBarrier(backBuffer, rhi::RenderTarget);
                for (const auto& pass : m_renderPasses)
                {
                    if(pass->startup())
                        pass->render(backBuffer, command);
                }
                command->addImageBarrier(backBuffer, rhi::Present);
            });

            m_device->runGarbageCollection();
        }

        m_device->waitIdle();
        m_renderPasses.clear();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(m_window);
        glfwTerminate();
    }

    void DesktopApp::loadScene(const fs::path& path)
    {
        const std::vector<char> blob = sys::readBlobFile(sys::ASSETS_DIR / path);

        flatbuffers::Verifier v(reinterpret_cast<const uint8_t* const>(blob.data()), blob.size());
        assert(scene::VerifySceneBuffer(v));
        auto scene = scene::GetScene(blob.data());

        m_meshBuffers.allocate(m_device, *scene->buffers());
        m_meshBuffers.load(*scene->meshes());
        //m_meshBuffers.allocate(m_device, m_texturePool, *scene->materials());
        /*m_meshList.installStaticScene(m_device, *scene->instances());
        m_renderGraph.addResource("instances", m_meshList.getInstanceBuffer());
        m_renderGraph.addResource("meshes", m_meshBuffers.getMeshBuffer());
        m_renderGraph.addResource("materials", m_meshBuffers.getSkinBuffer());
        m_renderGraph.addResource("textures", m_texturePool);*/
    }

    void DesktopApp::glfw_key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
    {
        if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        const auto app = static_cast<DesktopApp*>(glfwGetWindowUserPointer(window));
        if(key == GLFW_KEY_SPACE && action == GLFW_PRESS)
        {
            if(glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED)
            {
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                app->m_camera->lockMouse(true);
            }
            else
            {
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                app->m_camera->lockMouse(false);
            }
        }
        app->m_camera->handleKeyboard(key, action, 0.002);
    }

    void DesktopApp::glfw_mouse_callback(GLFWwindow* window, int button, int action, int mods)
    {
        double x, y;
        glfwGetCursorPos(window, &x, &y);

        if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS)
        {

        }
    }

    void DesktopApp::glfw_scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
    {

    }

    void DesktopApp::glfw_size_callback(GLFWwindow* window, int width, int height)
    {
        const auto app = static_cast<DesktopApp*>(glfwGetWindowUserPointer(window));
        app->resize(width, height);
    }
}