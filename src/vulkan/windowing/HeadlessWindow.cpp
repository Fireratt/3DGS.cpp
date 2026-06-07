#include "HeadlessWindow.h"

HeadlessWindow::HeadlessWindow(int width, int height)
    : width(static_cast<uint32_t>(width)), height(static_cast<uint32_t>(height)) {
}

VkSurfaceKHR HeadlessWindow::createSurface(std::shared_ptr<VulkanContext> context) {
    vk::HeadlessSurfaceCreateInfoEXT surfaceCreateInfo{};
    auto headlessSurface = context->instance->createHeadlessSurfaceEXT(surfaceCreateInfo);
    surface = static_cast<VkSurfaceKHR>(headlessSurface);
    return surface;
}

std::vector<std::string> HeadlessWindow::getRequiredInstanceExtensions() {
    return {VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME};
}

std::pair<uint32_t, uint32_t> HeadlessWindow::getFramebufferSize() const {
    return {width, height};
}

bool HeadlessWindow::tick() {
    return true;
}

bool HeadlessWindow::isFocused() {
    return true;
}
