#include <filesystem>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <vulkan/vulkan_core.h>
#include <libenvpp/env.hpp>

#include "3dgs.h"
#include "args.hxx"
#include "spdlog/spdlog.h"
namespace {
bool hasVulkanInstanceExtension(const char* extensionName) {
    uint32_t extensionCount = 0;
    if (vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr) != VK_SUCCESS) {
        return false;
    }

    std::vector<VkExtensionProperties> extensions(extensionCount);
    if (vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data()) != VK_SUCCESS) {
        return false;
    }

    for (const auto& extension: extensions) {
        if (std::strcmp(extension.extensionName, extensionName) == 0) {
            return true;
        }
    }

    return false;
}
}


int main(int argc, char** argv) {
    spdlog::set_pattern("[%H:%M:%S] [%^%L%$] %v");

    args::ArgumentParser parser("Vulkan Splatting");
    args::HelpFlag helpFlag{parser, "help", "Display this help menu", {'h', "help"}};
    args::Flag validationLayersFlag{
        parser, "validation-layers", "Enable Vulkan validation layers", {"validation"}
    };
    args::Flag verboseFlag{parser, "verbose", "Enable verbose logging", {'v', "verbose"}};
    args::ValueFlag<uint32_t> physicalDeviceIdFlag{
        parser, "physical-device", "Select physical device by index", {'d', "device"}
    };
    args::Flag immediateSwapchainFlag{
        parser, "immediate-swapchain", "Set swapchain mode to immediate (VK_PRESENT_MODE_IMMEDIATE_KHR)",
        {'i', "immediate-swapchain"}
    };
    args::ValueFlag<uint32_t> widthFlag{parser, "width", "Set window width", {'w', "width"}};
    args::ValueFlag<uint32_t> heightFlag{parser, "height", "Set window height", {'h', "height"}};
    args::Flag noGuiFlag{parser, "no-gui", "Disable GUI", { "no-gui"}};
#ifdef VKGS_ENABLE_HEADLESS
    args::Flag headlessFlag{parser, "headless", "Run without a visible window", {"headless"}};
#endif
    args::Flag tileHeatmapFlag{parser, "tile-heatmap", "Start with tile load heatmap enabled", {"tile-heatmap"}};
    args::Flag frameStatsFlag{parser, "frame-stats", "Collect per-frame Gaussian statistics", {"frame-stats"}};
    args::Flag printFrameStatsFlag{parser, "print-frame-stats", "Print per-frame Gaussian statistics", {"print-frame-stats"}};
    args::ValueFlag<std::string> statsCsvFlag{parser, "stats-csv", "Write per-frame Gaussian statistics CSV", {"stats-csv"}};
    args::ValueFlag<std::string> tileInstancesCsvFlag{parser, "tile-instances-csv", "Write first-frame per-tile Gaussian instance CSV", {"tile-instances-csv"}};
    args::Flag offscreenFlag{parser, "offscreen", "Enable offscreen rendering and dump frames to ./tmp", {"offscreen"}};
    args::Positional<std::string> scenePath{parser, "scene", "Path to scene file", "scene.ply"};
    args::Positional<std::string> cameraPath{parser, "trajectory", "Path to trajectory", ""};

    try {
        parser.ParseCLI(argc, argv);
    } catch (const args::Completion& e) {
        std::cout << e.what();
        return 0;
    }
    catch (const args::Help&) {
        std::cout << parser;
        return 0;
    }
    catch (const args::ParseError& e) {
        std::cout << e.what() << std::endl;
        std::cout << parser;
        return 1;
    }

    auto pre = env::prefix("VKGS");
    auto validationLayers = pre.register_variable<bool>("VALIDATION_LAYERS");
    auto physicalDeviceId = pre.register_variable<uint8_t>("PHYSICAL_DEVICE");
    auto immediateSwapchain = pre.register_variable<bool>("IMMEDIATE_SWAPCHAIN");
#ifdef VKGS_ENABLE_HEADLESS
    auto headless = pre.register_variable<bool>("HEADLESS");
#endif
    auto offscreen = pre.register_variable<bool>("OFFSCREEN");
    auto envVars = pre.parse_and_validate();

    if (args::get(verboseFlag)) {
        spdlog::set_level(spdlog::level::debug);
    }

#ifdef VKGS_ENABLE_HEADLESS
    auto enableHeadless = envVars.get_or(headless, false);
    if (headlessFlag) {
        enableHeadless = args::get(headlessFlag);
    }
#else
    constexpr bool enableHeadless = false;
#endif

    VulkanSplatting::RendererConfiguration config{
        envVars.get_or(validationLayers, false),
        envVars.get(physicalDeviceId).has_value()
            ? std::make_optional(envVars.get(physicalDeviceId).value())
            : std::nullopt,
        envVars.get_or(immediateSwapchain, false),
        args::get(scenePath),
        args::get(cameraPath)
    };
    config.enableOffscreen = envVars.get_or(offscreen, false);

    // check that the scene file exists
    if (!std::filesystem::exists(config.scene)) {
        spdlog::critical("File does not exist: {}", config.scene);
        return 0;
    }
    if (!std::filesystem::exists(config.trajectory)) {
        config.enableTrajectory = false ; 
        spdlog::info("Trajectory File does not exist: {} , disable trajectory moving", config.trajectory);
    }

    if (validationLayersFlag) {
        config.enableVulkanValidationLayers = args::get(validationLayersFlag);
    }

    if (physicalDeviceIdFlag) {
        config.physicalDeviceId = std::make_optional<uint8_t>(static_cast<uint8_t>(args::get(physicalDeviceIdFlag)));
    }

    if (immediateSwapchainFlag) {
        config.immediateSwapchain = args::get(immediateSwapchainFlag);
    }

    if (enableHeadless || noGuiFlag) {
    if (offscreenFlag) {
        config.enableOffscreen = args::get(offscreenFlag);
    }

    config.enableGui = !args::get(noGuiFlag);
    if (config.enableOffscreen) {
        config.enableGui = false;
    }

    config.showTileHeatmap = args::get(tileHeatmapFlag);
    config.enableFrameStats = args::get(frameStatsFlag);
    config.printFrameStats = args::get(printFrameStatsFlag);
    if (statsCsvFlag) {
        config.statsCsvPath = args::get(statsCsvFlag);
    }
    if (tileInstancesCsvFlag) {
        config.tileInstancesCsvPath = args::get(tileInstancesCsvFlag);
    }
    if (config.printFrameStats || !config.statsCsvPath.empty()) {
        config.enableFrameStats = true;
    }

    auto width = widthFlag ? args::get(widthFlag) : 1280;
    auto height = heightFlag ? args::get(heightFlag) : 720;
    try {

#ifdef VKGS_ENABLE_HEADLESS
    if (enableHeadless) {
        const char* backendEnv = std::getenv("VKGS_HEADLESS_BACKEND");
        std::string headlessBackend = backendEnv == nullptr ? "auto" : backendEnv;
        if (headlessBackend != "auto" && headlessBackend != "glfw" && headlessBackend != "surface") {
            throw std::runtime_error("VKGS_HEADLESS_BACKEND must be auto, glfw, or surface");
        }
        bool hasWindowSystem = std::getenv("DISPLAY") != nullptr || std::getenv("WAYLAND_DISPLAY") != nullptr;
        if (headlessBackend == "surface" && !hasVulkanInstanceExtension(VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME)) {
            throw std::runtime_error("VKGS_HEADLESS_BACKEND=surface requested, but VK_EXT_headless_surface is not exposed by this Vulkan runtime");
        }
        if (headlessBackend != "surface" && !hasWindowSystem) {
            throw std::runtime_error("Headless mode needs a presentable WSI surface. No DISPLAY/WAYLAND_DISPLAY is available for the default hidden-GLFW backend. Run the container with X11/Wayland or an NVIDIA Xorg dummy display, or set VKGS_HEADLESS_BACKEND=surface only on a Vulkan runtime that exposes VK_EXT_headless_surface.");
        }
        bool useHiddenGlfw = headlessBackend != "surface";
        if (useHiddenGlfw) {
            spdlog::info("Using hidden GLFW surface for headless mode");
            config.window = VulkanSplatting::createGlfwWindow("Vulkan Splatting", width, height, false);
        } else {
            spdlog::info("Using VK_EXT_headless_surface for headless mode");
            config.window = VulkanSplatting::createHeadlessWindow(width, height);
        }
    } else
#endif
    {
        config.window = VulkanSplatting::createGlfwWindow("Vulkan Splatting", width, height);
    }
    auto renderer = VulkanSplatting(config);
    renderer.start();
    } catch (const std::exception& e) {
        spdlog::critical(e.what());
        std::cout << e.what() << std::endl;
        return 1;
    }
    return 0;
}
