# 运动模糊（Motion Blur）无效分析报告

## 当前进度

| 步骤 | 状态 | 说明 |
|------|------|------|
| ① Backup Buffer 添加 SAMPLED_BIT | ✅ | main_camera.cpp:52-87 |
| ② 枚举修改 (vulkan_passes.h) | ✅ | attachment 7→6, subpass 7→5 |
| ③ setupRenderPass() 删减 | ⚠️ |**残留 dependencies[5][6]** |
| ④a draw() 简化 | ✅ | 单 framebuffer, ColorGrading 后结束 |
| ④b drawForward() 简化 | ❌ | 仍为旧签名 + 旧函数体 |
| ④c axis pipeline subpass | ❌ | line 1596 引用已删除枚举 |
| ⑤ PUIPass subpass 索引 | ✅ | init_info.Subpass=0 |
| ⑥ PCombineUIPass | ⚠️ | subpass=1 ✅, depth test 还是 TRUE |
| ⑦ MotionBlur 输出目标 | ✅ | 3 参数 + external imageView |
| ⑧ RP3 创建 | ❌ | 未开始 |
| ⑨ renderFrame() 编排 | ❌ | 仍为旧 6 参数调用 |
| ⑩ renderFrameForward() | ❌ | 同上 |
| ⑪ recreateSwapChain() | ❌ | initialize→updateAfterFramebufferRecreate 错误 |
| ⑫ initializeRenderPass() | ❌ | 未调用 RP3 创建 |
| ⑬ cleanup RP3 | ❌ | 未实现 |

---

## 当前编译错误（7 处）

| 文件 | 行 | 引用 | 修复 |
|------|-----|------|------|
| main_camera.cpp | 373 | `_main_camera_subpass_ui` | 删 dependencies[5] |
| main_camera.cpp | 385 | `_main_camera_subpass_ui` | 删 dependencies[6] |
| main_camera.cpp | 386 | `_main_camera_subpass_combine_ui` | 删 dependencies[6] |
| main_camera.cpp | 1596 | `_main_camera_subpass_ui` | 改 forward_lighting |
| main_camera.cpp | 2175 | `_main_camera_pass_swap_chain_image` | 删此行 |
| vulkan_manager.cpp | 132 | draw() 旧 6 参数 | 改 2 参数+RP3 |
| vulkan_manager.cpp | 265 | drawForward() 旧 6 参数 | 改 2 参数+RP3 |

---

## 步骤 ③：setupRenderPass 残留修复

**文件**: [main_camera.cpp:308,371-395](engine/source/runtime/function/render/source/vulkan_manager/passes/main_camera.cpp#L308)

```cpp
// 当前:
VkSubpassDependency dependencies[7] = {};
// ... dependencies[0]~[6] 共 7 个，其中 [5] 和 [6] 引用已删除的枚举

// 修复:
VkSubpassDependency dependencies[5] = {};
// 删除 dependencies[5] (color_grading→ui) 和 dependencies[6] (ui→combine_ui)
```

---

## 步骤 ④b：drawForward() 修复

**文件**: [main_camera.cpp:2153-2242](engine/source/runtime/function/render/source/vulkan_manager/passes/main_camera.cpp#L2153-L2242)

改为和 `draw()` 一样：2 参数签名，6 个 clear_values，ColorGrading 后直接 `vkCmdEndRenderPass`。删除 UI/Combine 子通道代码和 `_main_camera_pass_swap_chain_image` 引用。

---

## 步骤 ④c：Axis Pipeline 修复

**文件**: [main_camera.cpp:1596](engine/source/runtime/function/render/source/vulkan_manager/passes/main_camera.cpp#L1596)

```cpp
// 当前: pipelineInfo.subpass = _main_camera_subpass_ui;  // 编译错误

// 修复: pipelineInfo.subpass = _main_camera_subpass_forward_lighting;
// (Axis 之后在 RP1 Subpass 2 中渲染，或移到 RP3 Subpass 0)
```

---

## 步骤 ⑥：CombineUI depth test 修复

**文件**: [combine_ui.cpp:158-159](engine/source/runtime/function/render/source/vulkan_manager/passes/combine_ui.cpp#L158-L159)

```cpp
// 当前: depthTestEnable = VK_TRUE; depthWriteEnable = VK_TRUE;

// 修复: depthTestEnable = VK_FALSE; depthWriteEnable = VK_FALSE;
// RP3 无 depth attachment，depth test 必须关闭
```

---

## 步骤 ⑧：RP3 封装为独立函数

### vulkan_manager.h 声明

```cpp
private:
    void setupUICombineRenderPass();
    void setupUICombineFramebuffers();

    VkRenderPass               m_ui_combine_render_pass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_ui_combine_framebuffers;
```

### setupUICombineRenderPass() — 放 render_passes.cpp

只创建 VkRenderPass（一次调用）：

```cpp
void Pilot::PVulkanManager::setupUICombineRenderPass()
{
    VkAttachmentDescription attachments[3] = {};

    // [0] backup_odd — UI 写入 + CombineUI input 读
    attachments[0].format         = VK_FORMAT_R16G16B16A16_SFLOAT;
    attachments[0].samples        = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    attachments[0].finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // [1] backup_even — 运动模糊结果，CombineUI input 读
    attachments[1].format         = VK_FORMAT_R16G16B16A16_SFLOAT;
    attachments[1].samples        = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;   // 关键!
    attachments[1].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    attachments[1].finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // [2] swapchain — 最终输出
    attachments[2].format         = m_vulkan_context._swapchain_image_format;
    attachments[2].samples        = VK_SAMPLE_COUNT_1_BIT;
    attachments[2].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[2].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[2].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[2].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[2].finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // Subpass 0: UI
    VkAttachmentReference ui_color_ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription ui_sp = {};
    ui_sp.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    ui_sp.colorAttachmentCount = 1;
    ui_sp.pColorAttachments    = &ui_color_ref;

    // Subpass 1: CombineUI
    VkAttachmentReference combine_in_refs[2] = {
        {1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},  // backup_even→shader binding 0
        {0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},  // backup_odd →shader binding 1
    };
    VkAttachmentReference combine_color_ref = {2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription combine_sp = {};
    combine_sp.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    combine_sp.inputAttachmentCount = 2;
    combine_sp.pInputAttachments    = combine_in_refs;
    combine_sp.colorAttachmentCount = 1;
    combine_sp.pColorAttachments    = &combine_color_ref;

    VkSubpassDescription subpasses[2] = {ui_sp, combine_sp};

    VkSubpassDependency deps[3] = {};
    deps[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass    = 0;
    deps[0].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    deps[1].srcSubpass      = 0;
    deps[1].dstSubpass      = 1;
    deps[1].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].dstStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask   = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
    deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    deps[2].srcSubpass    = 1;
    deps[2].dstSubpass    = VK_SUBPASS_EXTERNAL;
    deps[2].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[2].dstStageMask  = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    deps[2].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rp = {};
    rp.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp.attachmentCount = 3;
    rp.pAttachments    = attachments;
    rp.subpassCount    = 2;
    rp.pSubpasses      = subpasses;
    rp.dependencyCount = 3;
    rp.pDependencies   = deps;

    if (vkCreateRenderPass(m_vulkan_context._device, &rp, nullptr,
                           &m_ui_combine_render_pass) != VK_SUCCESS)
        throw std::runtime_error("create UI combine render pass");
}
```

### setupUICombineFramebuffers() — 放 render_passes.cpp

每次 swapchain 重建时都需要调用：

```cpp
void Pilot::PVulkanManager::setupUICombineFramebuffers()
{
    m_ui_combine_framebuffers.resize(m_vulkan_context._swapchain_imageviews.size());
    for (size_t i = 0; i < m_vulkan_context._swapchain_imageviews.size(); i++)
    {
        VkImageView views[3] = {
            m_main_camera_pass.getFramebufferImageViews()[_main_camera_pass_backup_buffer_odd],
            m_main_camera_pass.getFramebufferImageViews()[_main_camera_pass_backup_buffer_even],
            m_vulkan_context._swapchain_imageviews[i],
        };
        VkFramebufferCreateInfo fb = {};
        fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb.renderPass = m_ui_combine_render_pass;
        fb.attachmentCount = 3;
        fb.pAttachments = views;
        fb.width  = m_vulkan_context._swapchain_extent.width;
        fb.height = m_vulkan_context._swapchain_extent.height;
        fb.layers = 1;
        if (vkCreateFramebuffer(m_vulkan_context._device, &fb, nullptr,
                                &m_ui_combine_framebuffers[i]) != VK_SUCCESS)
            throw std::runtime_error("create UI combine framebuffer");
    }
}
```

---

## 步骤 ⑨：renderFrame() 改为 3-RP

**文件**: [vulkan_manager.cpp:132](engine/source/runtime/function/render/source/vulkan_manager/vulkan_manager.cpp#L132)

```cpp
// RP1: 场景渲染
m_main_camera_pass.draw(m_color_grading_pass, m_tone_mapping_pass);

// RP2: 运动模糊
m_motion_blur_pass.draw();

// RP3: UI + CombineUI → Swapchain
{
    VkRenderPassBeginInfo rp = {};
    rp.sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass  = m_ui_combine_render_pass;
    rp.framebuffer = m_ui_combine_framebuffers[current_swapchain_image_index];
    rp.renderArea  = {{0, 0}, m_vulkan_context._swapchain_extent};

    VkClearValue cv[3] = {};
    cv[0].color = {{0,0,0,1}};
    cv[1].color = {{0,0,0,1}};
    cv[2].color = {{0,0,0,1}};
    rp.clearValueCount = 3;
    rp.pClearValues    = cv;

    m_vulkan_context._vkCmdBeginRenderPass(
        m_command_buffers[m_current_frame_index], &rp, VK_SUBPASS_CONTENTS_INLINE);

    m_ui_pass.draw(ui_state);

    m_vulkan_context._vkCmdNextSubpass(
        m_command_buffers[m_current_frame_index], VK_SUBPASS_CONTENTS_INLINE);

    m_combine_ui_pass.draw();

    m_vulkan_context._vkCmdEndRenderPass(m_command_buffers[m_current_frame_index]);
}
```

---

## 步骤 ⑩：renderFrameForward() 同样改为 3-RP

**文件**: [vulkan_manager.cpp:265](engine/source/runtime/function/render/source/vulkan_manager/vulkan_manager.cpp#L265)

```cpp
m_main_camera_pass.drawForward(m_color_grading_pass, m_tone_mapping_pass);
m_motion_blur_pass.draw();
// ... RP3 代码同步骤⑨ ...
```

---

## 步骤 ⑪：recreateSwapChain() 修复

**文件**: [swapchain.cpp:33-37](engine/source/runtime/function/render/source/vulkan_manager/misc/swapchain.cpp#L33-L37)

3 个修改：
1. Line 33: `m_motion_blur_pass.initialize(...)` → `updateAfterFramebufferRecreate(...)`
2. Line 37: CombineUI 参数 swap `(backup_odd, backup_even)` → `(backup_even, backup_odd)`
3. 新增：销毁旧 RP3 framebuffers + 调用 `setupUICombineFramebuffers()`

---

## 步骤 ⑫：initializeRenderPass() 调用 RP3

**文件**: [render_passes.cpp:42-44](engine/source/runtime/function/render/source/vulkan_manager/passes/render_passes.cpp#L42-L44)

```cpp
setupUICombineRenderPass();
setupUICombineFramebuffers();

m_ui_pass.initialize(m_ui_combine_render_pass);
m_combine_ui_pass.initialize(m_ui_combine_render_pass,
    m_main_camera_pass.getFramebufferImageViews()[_main_camera_pass_backup_buffer_even],
    m_main_camera_pass.getFramebufferImageViews()[_main_camera_pass_backup_buffer_odd]);
```

---

## 步骤 ⑬：cleanup 中销毁 RP3

```cpp
for (auto fb : m_ui_combine_framebuffers)
    vkDestroyFramebuffer(m_vulkan_context._device, fb, nullptr);
m_ui_combine_framebuffers.clear();
if (m_ui_combine_render_pass != VK_NULL_HANDLE)
    vkDestroyRenderPass(m_vulkan_context._device, m_ui_combine_render_pass, nullptr);
```

---

## 注意事项

1. `LOAD_OP_LOAD` 是关键的——RP3 attachment [1] 必须保留 RP2 的运动模糊结果
2. Render Pass 边界自动 layout transition，无需手动 barrier
3. 第一帧 `m_prev_proj_view_matrix` 为 identity，第二帧自动修复
4. `drawAxis()` 需要决定放 RP1 Subpass 2 还是 RP3 Subpass 0
5. combine_ui.frag **shader 不需要修改**（input_attachment_index 只在子 pass 内引用）
