#include "runtime/function/render/include/render/vulkan_manager/vulkan_manager.h"

// vulkan graphics render pass (m_vulkan_context._swapchain --> m_renderpass)
bool Pilot::PVulkanManager::initializeRenderPass()
{
    PRenderPassHelperInfo helper_info {};
    helper_info.p_context                = &m_vulkan_context;
    helper_info.descriptor_pool          = m_descriptor_pool;
    helper_info.p_global_render_resource = &m_global_render_resource;

    PMainCameraPass::setContext(helper_info);

    // initialize before lighting pass
    m_point_light_shadow_pass.initialize();
    m_directional_light_shadow_pass.initialize();
    PLightPassHelperInfo light_pass_helper_info {};
    light_pass_helper_info.point_light_shadow_color_image_view = m_point_light_shadow_pass.getFramebufferImageViews()[0];
    light_pass_helper_info.directional_light_shadow_color_image_view =
        m_directional_light_shadow_pass._framebuffer.attachments[0].view;
    m_main_camera_pass.setHelperInfo(light_pass_helper_info);
    m_main_camera_pass.initialize();
    setupUICombineRenderPass();
    setupUICombineFramebuffers();
    auto descriptor_layouts = m_main_camera_pass.getDescriptorSetLayouts();

    m_point_light_shadow_pass._per_mesh_layout       = descriptor_layouts[PMainCameraPass::LayoutType::_per_mesh];
    m_directional_light_shadow_pass._per_mesh_layout = descriptor_layouts[PMainCameraPass::LayoutType::_per_mesh];

    m_point_light_shadow_pass.postInitialize();
    m_directional_light_shadow_pass.postInitialize();

    m_tone_mapping_pass.initialize(m_main_camera_pass.getRenderPass(), m_main_camera_pass.getFramebufferImageViews()[_main_camera_pass_backup_buffer_odd]);

    m_color_grading_pass.initialize(m_main_camera_pass.getRenderPass(), m_main_camera_pass.getFramebufferImageViews()[_main_camera_pass_backup_buffer_even]);

    PMotionBlurPass::setContext(helper_info);
    m_motion_blur_pass.initialize(
        m_main_camera_pass.getFramebufferImageViews()[_main_camera_pass_backup_buffer_odd],
        m_main_camera_pass.getFramebufferImageViews()[_main_camera_pass_backup_buffer_even],
        m_global_render_resource._storage_buffer._global_upload_ringbuffer);

    m_ui_pass.initialize(m_ui_combine_render_pass);

    m_combine_ui_pass.initialize(m_ui_combine_render_pass, m_main_camera_pass.getFramebufferImageViews()[_main_camera_pass_backup_buffer_even], m_main_camera_pass.getFramebufferImageViews()[_main_camera_pass_backup_buffer_odd]);

    m_mouse_pick_pass._per_mesh_layout = descriptor_layouts[PMainCameraPass::LayoutType::_per_mesh];
    m_mouse_pick_pass.initialize();

    return true;
}


void Pilot::PVulkanManager::initializeUI(void *surface_ui)
{
    m_ui_pass.setSurfaceUI(surface_ui);
}

void Pilot::PVulkanManager::setupUICombineRenderPass()
{
    VkAttachmentDescription attachments[3] = {};

    attachments[0].format         = VK_FORMAT_R16G16B16A16_SFLOAT;
    attachments[0].samples        = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    attachments[0].finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    attachments[1].format         = VK_FORMAT_R16G16B16A16_SFLOAT;
    attachments[1].samples        = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;   // 关键!
    attachments[1].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    attachments[1].finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    attachments[2].format         = m_vulkan_context._swapchain_image_format;
    attachments[2].samples        = VK_SAMPLE_COUNT_1_BIT;
    attachments[2].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[2].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[2].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[2].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[2].finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

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

    VkSubpassDependency deps[4] = {};

    // external -> UI writes backup_odd
    deps[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass    = 0;
    deps[0].srcStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    // external MotionBlur output -> CombineUI reads backup_even
    deps[1].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].dstSubpass = 1;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
    deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    deps[2].srcSubpass      = 0;
    deps[2].dstSubpass      = 1;
    deps[2].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[2].dstStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[2].srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[2].dstAccessMask   = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
    deps[2].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    deps[3].srcSubpass    = 1;
    deps[3].dstSubpass    = VK_SUBPASS_EXTERNAL;
    deps[3].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[3].dstStageMask  = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    deps[3].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rp = {};
    rp.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp.attachmentCount = 3;
    rp.pAttachments    = attachments;
    rp.subpassCount    = 2;
    rp.pSubpasses      = subpasses;
    rp.dependencyCount = sizeof(deps) / sizeof(deps[0]);
    rp.pDependencies   = deps;

    if (vkCreateRenderPass(m_vulkan_context._device, &rp, nullptr,
                           &m_ui_combine_render_pass) != VK_SUCCESS)
        throw std::runtime_error("create UI combine render pass");
}

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
void Pilot::PVulkanManager::clearUICombineFramebuffers()
{
    for (VkFramebuffer framebuffer : m_ui_combine_framebuffers)
    {
        if (framebuffer != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(m_vulkan_context._device, framebuffer, nullptr);
        }
    }

    m_ui_combine_framebuffers.clear();
}

void Pilot::PVulkanManager::drawUICombinePass(uint32_t current_swapchain_image_index, void* ui_state)
{
    VkRenderPassBeginInfo renderpass_begin_info {};
    renderpass_begin_info.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderpass_begin_info.renderPass        = m_ui_combine_render_pass;
    renderpass_begin_info.framebuffer       = m_ui_combine_framebuffers[current_swapchain_image_index];
    renderpass_begin_info.renderArea.offset = {0, 0};
    renderpass_begin_info.renderArea.extent = m_vulkan_context._swapchain_extent;

    VkClearValue clear_values[3] = {};
    clear_values[0].color        = {{0.0f, 0.0f, 0.0f, 0.0f}};
    clear_values[1].color        = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clear_values[2].color        = {{0.0f, 0.0f, 0.0f, 1.0f}};

    renderpass_begin_info.clearValueCount = sizeof(clear_values) / sizeof(clear_values[0]);
    renderpass_begin_info.pClearValues    = clear_values;

    m_vulkan_context._vkCmdBeginRenderPass(
        m_command_buffers[m_current_frame_index], &renderpass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

    m_ui_pass.draw(ui_state);

    m_vulkan_context._vkCmdNextSubpass(m_command_buffers[m_current_frame_index], VK_SUBPASS_CONTENTS_INLINE);

    m_combine_ui_pass.draw();

    m_vulkan_context._vkCmdEndRenderPass(m_command_buffers[m_current_frame_index]);
}
