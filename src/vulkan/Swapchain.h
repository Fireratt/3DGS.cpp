#ifndef VULKAN_SPLATTING_SWAPCHAIN_H
#define VULKAN_SPLATTING_SWAPCHAIN_H


#include <memory>
#include "VulkanContext.h"
#include "Window.h"

class Swapchain {
public:
    Swapchain(const std::shared_ptr<VulkanContext> &context, const std::shared_ptr<Window> &window, bool immediate);

    vk::UniqueSwapchainKHR swapchain;
    vk::Extent2D swapchainExtent;
    std::vector<std::shared_ptr<Image>> swapchainImages;
    struct StagingBuffer {
        vk::Buffer buffer;
        VmaAllocation allocation;
        void* mapped;
    };

    std::vector<StagingBuffer> stagingBuffers;
    std::vector<vk::UniqueSemaphore> imageAvailableSemaphores;
    vk::SurfaceFormatKHR surfaceFormat;
    vk::Format swapchainFormat;
    vk::PresentModeKHR presentMode;
    uint32_t imageCount;

    void recreate();

    void DumpImage(const char* targetDirectory, int imageIndex,
            bool isBGRA = true);

private:
    std::shared_ptr<VulkanContext> context;
    std::shared_ptr<Window> window;

    bool immediate = false;

    void createSwapchain();

    void createSwapchainImages();

    void createOffscreenImages() ; 

    void createStagingBuffers() ; 

    static void writePPM(const std::string& path, uint8_t* data, uint32_t width, uint32_t height, bool bgra);
};


#endif //VULKAN_SPLATTING_SWAPCHAIN_H
