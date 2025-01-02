
#define GLFW_INCLUDE_NONE // Do not include any OpenGL/Vulkan headers
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>
#import <QuartzCore/QuartzCore.hpp>
#import <QuartzCore/CAMetalLayer.h>

void addLayerToWindow(GLFWwindow* window, CA::MetalLayer* layer)
{
    CAMetalLayer* native_layer = (__bridge CAMetalLayer*)layer;
    NSWindow* cocoa_window = glfwGetCocoaWindow(window);
    [native_layer setMaximumDrawableCount:3];
    cocoa_window.contentView.layer = native_layer;
    cocoa_window.contentView.wantsLayer = YES;
}