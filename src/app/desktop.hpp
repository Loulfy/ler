//
// Created by Loulfy on 04/12/2023.
//

#pragma once

#include "render/resource_mgr.hpp"
#include "camera/camera.hpp"
#include "render/graph.hpp"
#include "render/pass.hpp"
#include "rhi/rhi.hpp"
#include "sys/utils.hpp"

namespace ler::app
{
struct AppConfig
{
    rhi::GraphicsAPI api = rhi::GraphicsAPI::VULKAN;
    uint32_t width = 1920;
    uint32_t height = 1080;
    bool resizable = true;
    bool debug = true;
    bool vsync = true;
    bool msaa = true;
};

class DesktopApp
{
  public:
    explicit DesktopApp(AppConfig cfg = {});
    void run();

    // Non-copyable and non-movable
    DesktopApp(const DesktopApp&) = delete;
    DesktopApp(const DesktopApp&&) = delete;
    DesktopApp& operator=(const DesktopApp&) = delete;
    DesktopApp& operator=(const DesktopApp&&) = delete;

    // template<typename T>
    // void addPass() { m_renderPasses.emplace_back(std::make_shared<T>()); }
    // clang-format off
    template <typename T>
    void addPass(std::shared_ptr<T>& renderPass) { m_renderPasses.emplace_back(renderPass); }
    template<typename T>
    std::shared_ptr<T> addPass() { auto p = std::make_shared<T>(); m_renderPasses.emplace_back(p); return p; }
    void updateWindowIcon(const fs::path& path)const;
    void resize(int width, int height);
    // clang-format on

    void loadScene(const fs::path& path);

  private:
    static void glfw_key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void glfw_mouse_callback(GLFWwindow* window, int button, int action, int mods);
    static void glfw_scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
    static void glfw_size_callback(GLFWwindow* window, int width, int height);

    void notifyResize();

    GLFWwindow* m_window = nullptr;
    rhi::DevicePtr m_device;
    rhi::SwapChainPtr m_swapChain;
    rhi::BindlessTablePtr m_table;
    std::vector<rhi::SamplerPtr> m_samplers;
    std::vector<std::shared_ptr<rhi::IRenderPass>> m_renderPasses;
    std::vector<render::IMeshRenderer*> m_meshRenderer;
    render::ResourceManager m_resourceMgr;
    render::RenderMeshList* m_meshList = nullptr;
    cam::CameraPtr m_camera;
};
} // namespace ler::app