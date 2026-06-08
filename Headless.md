# 3DGS.cpp Docker 无头渲染方案

## 目标与硬约束

目标是在 Docker 镜像内运行 3DGS.cpp 做性能测试，不需要用户看到画面。

硬约束是不能改变现有 WSI/API 流：

- 运行时必须仍然创建 `VkSurfaceKHR`、`VkSwapchainKHR`。
- 每帧必须仍然调用 `vkAcquireNextImageKHR` / `vkAcquireNextImage2KHR` 等 acquire API。
- 每帧必须仍然调用 `vkQueuePresentKHR`。
- GVulkan-3DGS 必须能在 Vulkan layer/dispatch 链上看到这些调用。

因此不能把渲染主路径改成纯 offscreen image、storage image、`vkCmdCopyImageToBuffer` 后丢弃，或者跳过 present。这些方案虽然“无头”，但会破坏 GVulkan-3DGS 依赖的 acquire/present API 流。

## 推荐结论

采用“保留 swapchain 的无头 WSI”方案。

优先级如下：

1. 默认：`--headless` 使用隐藏 GLFW surface。它需要 X11/Wayland 或 Xorg dummy，但窗口不可见，仍然走真实 `VkSurfaceKHR`/`VkSwapchainKHR`/acquire/present。
2. 可选：显式设置 `VKGS_HEADLESS_BACKEND=surface` 时使用 `VK_EXT_headless_surface` 创建 headless `VkSurfaceKHR`。
3. Docker 中推荐连接 host X11/Wayland 或 NVIDIA Xorg dummy screen；普通 `Xvfb` 只有在 `vulkaninfo` 明确显示目标 GPU 且 swapchain/present 可用时才临时使用。

默认方案不改 `Renderer::draw()` 的 acquire-submit-present 顺序，也不改 `Swapchain` 的核心逻辑；只是让 GLFW 创建不可见窗口。`VK_EXT_headless_surface` 后端保留为兼容驱动上的显式选项。

## 现有代码路径

当前 Linux viewer 使用 GLFW：

- `apps/viewer/main.cpp` 调用 `VulkanSplatting::createGlfwWindow(...)`。
- `Renderer::initializeVulkan()` 调用 `window->getRequiredInstanceExtensions()`、`window->createSurface(context)`，随后 `selectPhysicalDevice(..., surface)`。
- `VulkanContext::selectPhysicalDevice()` 在有 surface 时启用 `VK_KHR_swapchain`。
- `Swapchain` 通过 `getSurfaceCapabilitiesKHR` / `getSurfaceFormatsKHR` / `getSurfacePresentModesKHR` 创建真实 swapchain。
- `Renderer::draw()` 中已经存在：
  - `context->device->acquireNextImageKHR(...)`
  - compute submit
  - render submit
  - `context->queues[VulkanContext::Queue::PRESENT].queue.presentKHR(...)`

要迁移到 Docker headless，应复用这条路径，而不是新增一条绕过 swapchain 的渲染路径。
当前代码支持：

- Linux/Unix 非 Apple 构建会启用 `VKGS_ENABLE_HEADLESS`。
- `--headless` 或 `VKGS_HEADLESS=1` 默认使用隐藏 GLFW surface；设置 `VKGS_HEADLESS_BACKEND=surface` 才使用 `HeadlessWindow` 创建 `VK_EXT_headless_surface`。
- headless 模式会自动关闭 GUI，避免进入 ImGui/GLFW 后端。
- `Renderer::draw()` 的 acquire-submit-present 顺序保持不变。
- 已验证 NVIDIA 550.90.07 在 `VK_EXT_headless_surface` 的 surface 查询阶段会段错；默认隐藏 GLFW 后端用于避开该驱动问题。
- 已验证 `peaceful_hellman` 容器内没有 `DISPLAY`/`WAYLAND_DISPLAY`、没有 `/dev/dri`、`vulkaninfo` 不暴露 `VK_EXT_headless_surface`，且 `VK_KHR_display` 查询 display/plane 数量为 0；该环境没有可创建 swapchain 的 WSI surface，必须挂载/启动 X11、Wayland 或 NVIDIA Xorg dummy。

## 显式后端：`VK_EXT_headless_surface`

### 设计

新增一个窗口实现，例如：

- `src/vulkan/windowing/HeadlessWindow.h`
- `src/vulkan/windowing/HeadlessWindow.cpp`

该类继承现有 `Window`，行为如下：

- `getRequiredInstanceExtensions()` 返回：
  - `VK_KHR_surface`
  - `VK_EXT_headless_surface`
- `createSurface()` 调用 `vkCreateHeadlessSurfaceEXT` 创建 `VkSurfaceKHR`。
- `getFramebufferSize()` 返回固定测试分辨率，例如 CLI 传入的 `width/height`。
- `getMouseButton()` / `getCursorTranslation()` / `getKeys()` 返回空输入。
- `mouseCapture()` 空实现。
- `isFocused()` 返回 `true`。
- `tick()` 在无头压测时一直返回 `true`；固定帧测试当前使用外层 `timeout`，后续可补 `--frames N`。

viewer 增加一个选择入口：

- `--headless` 或 `VKGS_HEADLESS=1` 默认使用隐藏 GLFW；`VKGS_HEADLESS_BACKEND=surface` 时使用 `createHeadlessWindow(...)`。
- headless 模式默认强制 `config.enableGui = false`，避免 `ImguiManager` 里把 `Window` 强转成 `GLFWWindow`。

### 保持不变的地方

以下逻辑不要改：

- `Renderer::draw()` 中的 acquire-submit-present 顺序。
- `Swapchain` 以 `VkSurfaceKHR` 创建 `VkSwapchainKHR` 的方式。
- `VulkanContext` 在有 surface 时启用 `VK_KHR_swapchain` 的方式。
- GPU profiling、query、render command buffer 的主体逻辑。

这样 GVulkan-3DGS 仍能看到真实的 `vkAcquireNextImageKHR` 和 `vkQueuePresentKHR` 调用。

### Docker 依赖

镜像内至少需要：

- Vulkan loader：`libvulkan1`
- Vulkan tools：`vulkan-tools`
- 编译依赖：`cmake`、`ninja-build` 或 `make`、`g++`
- shader 工具：`glslang-tools` 或对应 Vulkan SDK 中的 `glslangValidator`
- 项目依赖：`libglfw3-dev`、`libglm-dev`
- 如果启用 validation：`vulkan-validationlayers`

NVIDIA 容器运行时建议带上：

```bash
docker run --rm -it \
  --gpus all \
  -e NVIDIA_DRIVER_CAPABILITIES=compute,graphics,utility \
  <image> bash
```

进入容器后先检查 Vulkan loader、ICD 和扩展：

```bash
vulkaninfo --summary
vulkaninfo | grep -E "VK_EXT_headless_surface|deviceName|apiVersion"
```

如果目标 GPU 没有暴露 `VK_EXT_headless_surface`，不要继续使用方案 A，改用方案 B。

### 运行建议

默认 `VKGS_HEADLESS_BACKEND=auto` 使用隐藏 GLFW surface；如需强制 `VK_EXT_headless_surface`，在命令前设置 `VKGS_HEADLESS_BACKEND=surface`。

```bash
VKGS_HEADLESS=1 \
VK_INSTANCE_LAYERS=<GVulkan-3DGS layer name> \
./build/apps/viewer/3dgs_viewer \
  --headless \
  --no-gui \
  --immediate-swapchain \
  --width 1280 \
  --height 720 \
  --stats-csv profile.csv \
  <scene.ply> \
  <trajectory.json>
```

如果还没有实现固定帧退出，可先用外层 `timeout` 控制测试时长：

```bash
timeout 30s ./build/apps/viewer/3dgs_viewer --headless --no-gui --width 1280 --height 720 <scene.ply> <trajectory.json>
```

后续建议补一个 `--frames N`，这样性能测试可以避开启动期，并精确统计固定帧数。

## 方案 B：Docker 内保留 GLFW，但连接无显示需求的 WSI 后端

这个方案对 3DGS.cpp 改动最少，甚至可以零代码改动。核心是：让 GLFW 仍能创建一个平台 window/surface，但这个 window 不需要被人看到。

可选后端：

- host X11：挂载 `/tmp/.X11-unix`，传入 `DISPLAY`。
- host Wayland：挂载 Wayland socket，传入 `WAYLAND_DISPLAY` 和 `XDG_RUNTIME_DIR`。
- NVIDIA Xorg dummy screen：容器或宿主机启动一个绑定 NVIDIA 驱动的 Xorg display，例如 `:99`。

注意：已经启动的容器不能追加 bind mount；若容器启动时没有挂载 X11/Wayland socket，需要重新 `docker run` 或用 compose 重建。

host X11 示例：

```bash
docker run --rm -it \
  --gpus all \
  -e NVIDIA_DRIVER_CAPABILITIES=compute,graphics,utility \
  -e DISPLAY=$DISPLAY \
  -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
  <image> bash
```

运行：

```bash
VK_INSTANCE_LAYERS=<GVulkan-3DGS layer name> \
./build/apps/viewer/3dgs_viewer \
  --no-gui \
  --immediate-swapchain \
  --width 1280 \
  --height 720 \
  --stats-csv profile.csv \
  <scene.ply> \
  <trajectory.json>
```

```

为了避免弹出可见窗口，可以在 GLFW 后端增加 headless/hidden 模式：

```cpp
glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
```

该改动只影响窗口可见性，不改变 Vulkan surface、swapchain、acquire 或 present。

## 性能测试注意事项

- 使用 `--no-gui`，避免 ImGui 和输入事件影响测试。
- 固定 `--width` / `--height`，保证 tile 数、swapchain extent 和 shader workgroup 一致。
- 使用固定相机轨迹文件，避免人工输入影响帧间负载。
- 使用 `--immediate-swapchain` 关闭可能的 v-sync 限制；如果日志显示实际 present mode 仍是 FIFO，需要在结果中记录，因为 FPS 可能被 WSI 限制。
- 如果目的是测试 GPU 计算段，可以同时看 GPU timestamp/query 输出和 CPU frame time，避免把 display server 的 present 等待误判为 shader 性能。
- 每次结果记录 Vulkan 设备名、driver 版本、present mode、分辨率、scene、trajectory、容器镜像 tag。

## 验证清单

启动前：

```bash
vulkaninfo --summary
```

确认：

- device 是目标 GPU，不是 llvmpipe/lavapipe 等软件实现。
- 使用方案 A 时能看到 `VK_EXT_headless_surface`。
- 使用方案 B 时，`DISPLAY` 或 Wayland socket 可用。

运行时打开 verbose 日志：

```bash
./build/apps/viewer/3dgs_viewer --verbose --no-gui --immediate-swapchain --width 1280 --height 720 <scene.ply> <trajectory.json>
```

```

确认日志中至少有：

- Vulkan physical device 选择正确。
- Surface format 正常。
- Present mode 已记录。
- Swapchain extent 是预期分辨率。
- 持续输出 frame time / FPS。

GVulkan-3DGS 侧确认捕获到：

- `vkCreateSwapchainKHR`
- `vkAcquireNextImageKHR`
- `vkQueueSubmit`
- `vkQueuePresentKHR`

如果 GVulkan-3DGS 没有看到 acquire/present，说明走错了路径，必须停止测试并检查是否误用了 offscreen/headless compute-only 路径。

## 风险与回退

- `VK_EXT_headless_surface` 不是所有驱动都保证可用；缺失时用方案 B。
- `Xvfb` 可能不支持目标 GPU 的 Vulkan present，不能默认作为性能基线。
- FIFO present mode 会引入同步限制，性能结果需要单独标注。
- headless 模式若启用 GUI 会触发 `ImguiManager` 对 `GLFWWindow` 的假设，应强制 `--no-gui`。
- 如果容器中 `vulkaninfo` 看不到目标 GPU，优先检查 NVIDIA container runtime、`NVIDIA_DRIVER_CAPABILITIES=graphics`、ICD 文件和宿主驱动挂载。

## 实施步骤

1. 先在当前 GLFW 路径上用方案 B 跑通 Docker，验证 GVulkan-3DGS 能捕获 acquire/present。
2. 新增 `HeadlessWindow` 和 `--headless`，实现方案 A。
3. 在方案 A 下验证 `vulkaninfo`、swapchain 创建、`vkAcquireNextImageKHR`、`vkQueuePresentKHR`。
4. 增加 `--frames N` 或等价环境变量，固定性能测试帧数。
5. 固化 Dockerfile 和运行脚本，默认使用 `--headless --no-gui --immediate-swapchain`；若扩展不可用，脚本自动提示改用 X11/Xorg dummy。
