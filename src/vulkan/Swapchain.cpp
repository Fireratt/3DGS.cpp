#include "Swapchain.h"

#include "glm/glm.hpp"
#include <fstream>
#include <filesystem>
#include "spdlog/spdlog.h"
#include <vk_enum_string_helper.h>

Swapchain::Swapchain(const std::shared_ptr<VulkanContext>& context, const std::shared_ptr<Window>& window,
                     bool immediate) : context(context), window(window), immediate(immediate) {
    auto createSwapchainImages = [&](){this->createOffscreenImages() ; } ; 

    createSwapchain();
    createStagingBuffers() ;
    createSwapchainImages();
}

void Swapchain::createSwapchain() {
    auto physicalDevice = context->physicalDevice;

    auto capabilities = physicalDevice.getSurfaceCapabilitiesKHR(*context->surface.value());
    auto formats = physicalDevice.getSurfaceFormatsKHR(*context->surface.value());
    auto presentModes = physicalDevice.getSurfacePresentModesKHR(*context->surface.value());

    auto [width, height] = window->getFramebufferSize();

    surfaceFormat = formats[0];
    for (const auto& availableFormat: formats) {
        if (availableFormat.format == vk::Format::eB8G8R8A8Unorm &&
            availableFormat.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
            surfaceFormat = availableFormat;
            break;
        }
    }
    spdlog::debug("Surface format: {}", string_VkFormat(static_cast<VkFormat>(surfaceFormat.format)));

    presentMode = vk::PresentModeKHR::eFifo;
    for (const auto& availablePresentMode: presentModes) {
        if (immediate && availablePresentMode == vk::PresentModeKHR::eImmediate) {
            presentMode = availablePresentMode;
            break;
        }

        if (availablePresentMode == vk::PresentModeKHR::eMailbox) {
            presentMode = availablePresentMode;
            break;
        }
    }
    spdlog::debug("Present mode: {}", string_VkPresentModeKHR(static_cast<VkPresentModeKHR>(presentMode)));

    auto extent = capabilities.currentExtent;
    if (capabilities.currentExtent.width == UINT32_MAX || capabilities.currentExtent.width == 0) {
        extent.width = std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        extent.height = std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }

    spdlog::debug("Swapchain extent range: {}x{} - {}x{}", capabilities.minImageExtent.width, capabilities.minImageExtent.height,
                  capabilities.maxImageExtent.width, capabilities.maxImageExtent.height);

    imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    } else if (capabilities.maxImageCount == 0) {
        imageCount = capabilities.minImageCount;
    }

    vk::SwapchainCreateInfoKHR createInfo = {};
    createInfo.surface = *context->surface.value();
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eStorage;

    std::vector<uint32_t> uniqueQueueFamilies;
    for (auto& queue: context->queues) {
        if (std::find(uniqueQueueFamilies.begin(), uniqueQueueFamilies.end(), queue.first) ==
            uniqueQueueFamilies.end()) {
            uniqueQueueFamilies.push_back(queue.first);
        }
    }

    if (uniqueQueueFamilies.size() > 1) {
        createInfo.imageSharingMode = vk::SharingMode::eConcurrent;
        createInfo.queueFamilyIndexCount = (uint32_t) uniqueQueueFamilies.size();
        createInfo.pQueueFamilyIndices = uniqueQueueFamilies.data();
    } else {
        createInfo.imageSharingMode = vk::SharingMode::eExclusive;
    }

    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;

    spdlog::debug("Swapchain extent: {}x{}. Preferred extent: {}x{}", extent.width, extent.height, width, height);
    swapchainExtent = extent;
    swapchainFormat = surfaceFormat.format;

    swapchain = context->device->createSwapchainKHRUnique(createInfo);
    spdlog::debug("Swapchain created");
}

void Swapchain::createSwapchainImages() {
    auto images = context->device->getSwapchainImagesKHR(*swapchain);

    for (auto& image: images) {
        auto imageView = context->device->createImageViewUnique({
            {}, image, vk::ImageViewType::e2D,
            swapchainFormat, {},
            {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
        });
        swapchainImages.push_back(std::make_shared<Image>(
                image,
                std::move(imageView),
                swapchainFormat,
                swapchainExtent,
                std::nullopt
            )
        );
        
    }

    for (int i = 0; i < swapchainImages.size(); i++) {
        imageAvailableSemaphores.emplace_back(context->device->createSemaphoreUnique({}));
    }
}
void Swapchain::createOffscreenImages() {

    swapchainImages.clear();
    imageAvailableSemaphores.clear();

    vk::Format format = swapchainFormat;
    vk::Extent2D extent = swapchainExtent;

    uint32_t imageCount = 3; // 保持和原 swapchain 类似的 buffering 数量

    for (uint32_t i = 0; i < imageCount; i++) {

        vk::ImageCreateInfo imageInfo{
            {},
            vk::ImageType::e2D,
            format,
            vk::Extent3D{extent.width, extent.height, 1},
            1, 1,
            vk::SampleCountFlagBits::e1,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eColorAttachment |
            vk::ImageUsageFlagBits::eTransferSrc |   // 用于 readback
            vk::ImageUsageFlagBits::eTransferDst |
            vk::ImageUsageFlagBits::eSampled |    // 可选（例如清空/拷贝）
            vk::ImageUsageFlagBits::eStorage,
            vk::SharingMode::eExclusive,
            {},
            vk::ImageLayout::eUndefined
        };
        
        VmaAllocationCreateInfo allocCreateInfo{};
        allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        VkImage rawImage;
        VmaAllocation allocation;

        vmaCreateImage(
            context->allocator,
            reinterpret_cast<VkImageCreateInfo*>(&imageInfo),
            &allocCreateInfo,
            &rawImage,
            &allocation,
            nullptr
        );
        vk::Image image(rawImage);
        auto imageView = context->device->createImageViewUnique({
            {},
            image,
            vk::ImageViewType::e2D,
            format,
            {},
            {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
        });

        swapchainImages.push_back(std::make_shared<Image>(
            image,
            std::move(imageView),
            format,
            extent
        ));
        swapchainImages.back()->allocation = allocation ;
        swapchainImages.back()->allocator = context->allocator ; 
        imageAvailableSemaphores.emplace_back(
            context->device->createSemaphoreUnique({})
        );
        
    }
}
void Swapchain::createStagingBuffers() {
    uint32_t imageCount = 3; // 保持和原 swapchain 类似的 buffering 数量
    vk::Extent2D extent = swapchainExtent;
    VkDeviceSize width = extent.width ; 
    VkDeviceSize height = extent.height ; 
    VkDeviceSize size = width * height * 4; // RGBA

    stagingBuffers.resize(imageCount);

    for (uint32_t i = 0; i < imageCount; i++) {

        vk::BufferCreateInfo bufferInfo{
            {},
            size,
            vk::BufferUsageFlagBits::eTransferDst,
            vk::SharingMode::eExclusive
        };

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VkBuffer rawBuffer;
        VmaAllocation allocation;
        VmaAllocationInfo allocDetail;

        vmaCreateBuffer(
            context->allocator,
            reinterpret_cast<VkBufferCreateInfo*>(&bufferInfo),
            &allocInfo,
            &rawBuffer,
            &allocation,
            &allocDetail
        );

        stagingBuffers[i].buffer = vk::Buffer(rawBuffer);
        stagingBuffers[i].allocation = allocation;
        stagingBuffers[i].mapped = allocDetail.pMappedData;
    }
}
void Swapchain::recreate() {
    auto createSwapchainImages = [&](){this->createOffscreenImages() ; } ; 


    context->device->waitIdle();
    swapchain.reset();
    swapchainImages.clear();

    createSwapchain();
    createSwapchainImages();
    spdlog::debug("Swapchain recreated");
}
// 简单写 PPM（无依赖，RGB）
void Swapchain::writePPM(const std::string& path, uint8_t* data, uint32_t width, uint32_t height, bool bgra) {
    std::ofstream ofs(path, std::ios::binary);
    ofs << "P6\n" << width << " " << height << "\n255\n";

    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint8_t* p = data + (y * width + x) * 4;

            uint8_t r = bgra ? p[2] : p[0];
            uint8_t g = p[1];
            uint8_t b = bgra ? p[0] : p[2];

            ofs.write((char*)&r, 1);
            ofs.write((char*)&g, 1);
            ofs.write((char*)&b, 1);
        }
    }
}
void Swapchain::DumpImage(const char* targetDirectory, int imageIndex,
               bool isBGRA) {
    static std::atomic<int> currentDumpedNum = 0 ; 
    vk::Extent2D extent = swapchainExtent;
    VkDeviceSize width = extent.width ; 
    VkDeviceSize height = extent.height ; 

    if (imageIndex < 0 || imageIndex >= stagingBuffers.size()) return;

    uint8_t* data = reinterpret_cast<uint8_t*>(
        stagingBuffers[imageIndex].mapped
    );

    if (!data) return;

    std::filesystem::create_directories(targetDirectory);

    std::ostringstream oss;
    oss << targetDirectory << "/frame_"
        << std::setw(6) << std::setfill('0') << currentDumpedNum.fetch_add(1)
        << ".ppm";

    writePPM(oss.str(), data, width, height, isBGRA);
}