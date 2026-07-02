# Motion Blur 实现进度校验

更新日期：2026-07-02

本文按当前源码状态校验 motion blur 接入进度，并给出后续修改顺序。当前实现已经有 motion blur pass、shader、UBO 同步和部分 render pass 拆分，但主渲染链路还没有完整接通，当前状态更接近“半成品/编译修复阶段”。

## 总体结论

| 维度 | 估计进度 | 说明 |
| --- | ---: | --- |
| 资源准备 | 70% | backup buffer 已可采样，depth 也已有 sampled usage |
| MotionBlur pass 雏形 | 55% | C++/shader 已存在，但有声明不一致和语法错误 |
| 主渲染链路接入 | 35% | MainCamera -> MotionBlur -> UI/CombineUI -> Swapchain 尚未真正串起来 |
| Swapchain 重建/清理 | 25% | recreate 和 cleanup 还缺 motion blur/RP3 资源生命周期处理 |
| 可运行效果 | 0%-10% | 当前存在编译级阻塞，尚不能稳定看到效果 |

整体完成度约 **45%**。下一阶段优先目标不是调 shader 效果，而是先让项目能编译，并让 motion blur 输出真正进入最终 swapchain。

## 已完成项

| 项目 | 状态 | 位置/说明 |
| --- | --- | --- |
| backup odd/even 添加 `VK_IMAGE_USAGE_SAMPLED_BIT` | 已完成 | `main_camera.cpp:85-86` |
| MainCamera attachment/subpass 数量缩减 | 基本完成 | `vulkan_passes.h` 已无 UI/CombineUI subpass |
| MainCamera render pass dependencies 缩减为 5 个 | 已完成 | `main_camera.cpp:308` |
| Axis pipeline subpass 修复 | 已完成 | 当前使用 `_main_camera_subpass_forward_lighting` |
| `MotionBlurUBO` 定义 | 已完成 | `vulkan_context.h` |
| 当前/上一帧 VP 矩阵同步 | 已完成 | `sync/scene.cpp` |
| `motion_blur.frag` | 已完成雏形 | 屏幕空间基于 depth 重建 world position |
| `PMotionBlurPass` 文件 | 已完成雏形 | `passes/motion_blur.cpp` |
| RP3 UI+CombineUI render pass 代码 | 已写入但未正确接入 | `render_passes.cpp` |
| CombineUI depth test | 已修复 | 当前 `depthTestEnable/depthWriteEnable` 均为 `VK_FALSE` |

## 当前阻塞问题

这些问题需要先修，否则后续效果调试没有意义。

### 1. `PMotionBlurPass::setupAttachments` 声明/定义不一致

文件：

- `engine/source/runtime/function/render/include/render/vulkan_manager/vulkan_passes.h`
- `engine/source/runtime/function/render/source/vulkan_manager/passes/motion_blur.cpp`

当前头文件声明：

```cpp
void setupAttachments();
```

当前 cpp 定义：

```cpp
void PMotionBlurPass::setupAttachments(VkImageView output_attachment)
```

并且 `initialize()` 中还有语法错误：

```cpp
setupAttachments(VkImageView output_attachment);
```

应改为：

```cpp
// vulkan_passes.h
void setupAttachments(VkImageView output_attachment);

// motion_blur.cpp
setupAttachments(output_attachment);
```

### 2. `renderFrame()` 仍调用旧版 MainCamera draw

文件：

- `engine/source/runtime/function/render/source/vulkan_manager/vulkan_manager.cpp`

当前仍然调用：

```cpp
m_main_camera_pass.draw(
    m_color_grading_pass,
    m_tone_mapping_pass,
    m_ui_pass,
    m_combine_ui_pass,
    current_swapchain_image_index,
    ui_state);
```

但当前 `PMainCameraPass::draw()` 已改为 2 参数：

```cpp
void draw(PColorGradingPass& color_grading_pass,
          PToneMappingPass& tone_mapping_pass);
```

应改为 3-RP 流程：

```cpp
// RP1: Scene -> ToneMapping -> ColorGrading
m_main_camera_pass.draw(m_color_grading_pass, m_tone_mapping_pass);

// RP2: Motion Blur
m_motion_blur_pass.draw();

// RP3: UI -> CombineUI -> Swapchain
drawUICombinePass(current_swapchain_image_index, ui_state);
```

可以先不抽函数，直接在 `renderFrame()` 内写 RP3 begin/next/end，等跑通后再整理。

### 3. `drawForward()` 仍是旧接口

文件：

- `engine/source/runtime/function/render/source/vulkan_manager/passes/main_camera.cpp`
- `engine/source/runtime/function/render/include/render/vulkan_manager/vulkan_passes.h`

头文件里 `drawForward()` 是 2 参数，但 cpp 里仍是 6 参数，并且还引用已删除的：

```cpp
_main_camera_pass_swap_chain_image
```

应把 `drawForward()` 改成和 `draw()` 一样，只负责 MainCamera render pass，到 ColorGrading 后结束 render pass。UI/CombineUI 统一放到 RP3。

### 4. `setupUICombineFramebuffers()` 重复定义

当前存在两份定义：

- `render_passes.cpp`
- `vulkan_manager.cpp`

应只保留一份。建议保留在 `render_passes.cpp`，删除 `vulkan_manager.cpp` 文件末尾重复的实现。

### 5. RP3 初始化顺序错误

当前 `initializeRenderPass()` 中先调用了：

```cpp
setupUICombineRenderPass();
setupUICombineFramebuffers();
```

然后才调用：

```cpp
m_main_camera_pass.initialize();
```

这会导致 RP3 framebuffer 依赖的 backup image view 还没有创建。正确顺序应是：

```cpp
m_main_camera_pass.initialize();

setupUICombineRenderPass();
setupUICombineFramebuffers();

m_ui_pass.initialize(m_ui_combine_render_pass);
m_combine_ui_pass.initialize(
    m_ui_combine_render_pass,
    m_main_camera_pass.getFramebufferImageViews()[_main_camera_pass_backup_buffer_even],
    m_main_camera_pass.getFramebufferImageViews()[_main_camera_pass_backup_buffer_odd]);
```

注意：这里 CombineUI 的 scene 输入应是 motion blur 输出，即 backup even；UI 输入是 backup odd。

### 6. UI/CombineUI 仍绑定 MainCamera render pass

当前代码：

```cpp
m_ui_pass.initialize(m_main_camera_pass.getRenderPass());
m_combine_ui_pass.initialize(m_main_camera_pass.getRenderPass(), ...);
```

应改为：

```cpp
m_ui_pass.initialize(m_ui_combine_render_pass);
m_combine_ui_pass.initialize(m_ui_combine_render_pass, ...);
```

否则 UI/CombineUI pipeline 的 subpass index 和 render pass 不匹配。

### 7. MotionBlur 输出没有进入 swapchain

当前 `m_motion_blur_pass.draw()` 在 main camera 后执行，但后面没有 RP3 将 motion blur 输出合成到 swapchain。最终 present 的 swapchain image 没有被正确写入，motion blur 结果也不会显示。

需要在 motion blur 后执行 UI+CombineUI：

```cpp
VkRenderPassBeginInfo rp = {};
rp.renderPass  = m_ui_combine_render_pass;
rp.framebuffer = m_ui_combine_framebuffers[current_swapchain_image_index];
...

vkCmdBeginRenderPass(...);
m_ui_pass.draw(ui_state);
vkCmdNextSubpass(...);
m_combine_ui_pass.draw();
vkCmdEndRenderPass(...);
```

### 8. Depth layout 需要给 MotionBlur shader 读取

motion blur shader 使用 sampler 读取 depth：

```glsl
layout(binding = 1) uniform sampler2D in_DepthTex;
```

但 MainCamera depth attachment 当前 final layout 是：

```cpp
VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
```

如果直接跨 render pass 作为 sampler 读取，应该改成：

```cpp
VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
```

或者在 MainCamera RP 结束后、MotionBlur RP 开始前手动加 image barrier。为了先跑通，建议优先改 final layout。

## 建议修改顺序

### 第一步：修编译错误

1. 修 `PMotionBlurPass::setupAttachments` 声明/调用。
2. 修改 `renderFrame()` 为 2 参数 MainCamera draw。
3. 修改 `renderFrameForward()` 为 2 参数 MainCamera drawForward。
4. 修改 `drawForward()` cpp 签名，删除 `_main_camera_pass_swap_chain_image`。
5. 删除重复的 `setupUICombineFramebuffers()`。

这一阶段目标：先让 C++ 能编译。

### 第二步：接通 3-RP 渲染链

目标链路：

```text
RP1 MainCamera:
  BasePass -> DeferredLighting -> ForwardLighting -> ToneMapping -> ColorGrading
  output: backup_odd

RP2 MotionBlur:
  input:  backup_odd + depth + MotionBlurUBO
  output: backup_even

RP3 UICombine:
  Subpass 0: UI -> backup_odd
  Subpass 1: CombineUI reads backup_even + backup_odd -> swapchain
```

需要改：

1. `initializeRenderPass()` 中 RP3 创建顺序。
2. `m_ui_pass.initialize()` 使用 `m_ui_combine_render_pass`。
3. `m_combine_ui_pass.initialize()` 使用 `m_ui_combine_render_pass`。
4. `renderFrame()` 和 `renderFrameForward()` 在 `m_motion_blur_pass.draw()` 后执行 RP3。

### 第三步：修 swapchain recreate

当前 `recreateSwapChain()` 中重新调用了：

```cpp
m_motion_blur_pass.initialize(...);
```

这会重复创建 descriptor set layout、pipeline、render pass 等资源。更合理的做法：

1. 初始化阶段只 `initialize()` 一次。
2. swapchain recreate 时销毁并重建 motion blur framebuffer。
3. 更新 motion blur descriptor：

```cpp
m_motion_blur_pass.updateAfterFramebufferRecreate(
    m_main_camera_pass.getFramebufferImageViews()[_main_camera_pass_backup_buffer_odd],
    m_global_render_resource._storage_buffer._global_upload_ringbuffer);
```

同时需要：

1. 销毁旧 `m_ui_combine_framebuffers`。
2. `setupUICombineFramebuffers()` 重建 RP3 framebuffers。
3. `m_combine_ui_pass.updateAfterFramebufferRecreate(backup_even, backup_odd)`。

### 第四步：补 cleanup

需要清理：

```cpp
for (VkFramebuffer framebuffer : m_ui_combine_framebuffers)
{
    vkDestroyFramebuffer(m_vulkan_context._device, framebuffer, nullptr);
}
m_ui_combine_framebuffers.clear();

if (m_ui_combine_render_pass != VK_NULL_HANDLE)
{
    vkDestroyRenderPass(m_vulkan_context._device, m_ui_combine_render_pass, nullptr);
    m_ui_combine_render_pass = VK_NULL_HANDLE;
}
```

Motion blur pass 自己的 render pass/framebuffer/pipeline/layout/descriptor layout 也应在统一 render pass cleanup 中销毁。当前项目整体 render pass cleanup 本来就比较弱，可以先补关键 framebuffer/render pass，后续再系统整理。

### 第五步：调 shader 效果

当前 shader 已经能表达 camera motion blur，但建议做这些修正：

1. UV 采样 clamp，避免越界采样：

```glsl
vec2 sampleUV = clamp(out_UV - velocity * t, vec2(0.0), vec2(1.0));
```

2. 天空/远平面不做模糊：

```glsl
if (depth >= 0.999)
{
    out_color = texture(in_ColorTex, out_UV);
    return;
}
```

3. 限制速度时先判断长度，避免 normalize 零向量。
4. 后续把 `blurScale`、`maxVelocity`、`numSamples` 做成可配置项。

## 推荐的最小可用实现目标

先实现 **camera motion blur**，不要一开始就做 per-object motion blur。

最小目标：

1. 相机快速移动/旋转时，画面出现屏幕空间拖影。
2. UI 不被模糊。
3. 静止相机时画面基本不变。
4. swapchain resize 后不崩溃。

暂不覆盖：

1. 角色骨骼动画自身运动模糊。
2. 单个物体移动但相机不动时的真实 motion blur。
3. 透明物体/粒子精确速度。

如果后面要做完整 per-object motion blur，需要新增 velocity buffer，或至少保存上一帧 model matrix，并在 geometry pass 输出每个像素速度。

## 当前重点文件清单

| 文件 | 后续动作 |
| --- | --- |
| `passes/motion_blur.cpp` | 修编译错误，补 framebuffer recreate 支持 |
| `vulkan_passes.h` | 修 `PMotionBlurPass` 私有函数声明 |
| `vulkan_manager.cpp` | 改 3-RP draw 编排，删除重复函数 |
| `render_passes.cpp` | 调整初始化顺序，保留 RP3 创建逻辑 |
| `swapchain.cpp` | 改 recreate 逻辑，重建 RP3 framebuffer，更新 descriptors |
| `main_camera.cpp` | 修 `drawForward()`，调整 depth final layout |
| `combine_ui.cpp` | 已基本正确，确认 subpass=1 和输入顺序 |
| `motion_blur.frag` | 跑通后优化 UV/depth/速度限制 |
