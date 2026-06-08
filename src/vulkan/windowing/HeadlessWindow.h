#ifndef HEADLESSWINDOW_H
#define HEADLESSWINDOW_H

#include "../Window.h"

class HeadlessWindow final : public Window {
public:
    HeadlessWindow(int width, int height);

    VkSurfaceKHR createSurface(std::shared_ptr<VulkanContext> context) override;

    std::vector<std::string> getRequiredInstanceExtensions() override;

    [[nodiscard]] std::pair<uint32_t, uint32_t> getFramebufferSize() const override;

    bool tick() override;

    bool isFocused() override;

private:
    uint32_t width;
    uint32_t height;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
};

#endif //HEADLESSWINDOW_H
