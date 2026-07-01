#include "runtime/function/render/include/render/vulkan_manager/vulkan_manager.h"

#include <vector>

bool Pilot::PVulkanManager::recreateSwapChain()
{
    int width = 0, height = 0;
    glfwGetFramebufferSize(m_vulkan_context._window, &width, &height);
    while (width == 0 || height == 0) // minimized 0,0 ,pause for now
    {
        glfwGetFramebufferSize(m_vulkan_context._window, &width, &height);
        glfwWaitEvents();
    }
    PRenderPassHelperInfo helper_info {};
    helper_info.p_context                = &m_vulkan_context;
    helper_info.descriptor_pool          = m_descriptor_pool;
    helper_info.p_global_render_resource = &m_global_render_resource;

    VkResult res_wait_for_fences = m_vulkan_context._vkWaitForFences(
        m_vulkan_context._device, m_max_frames_in_flight, m_is_frame_in_flight_fences, VK_TRUE, UINT64_MAX);
    assert(VK_SUCCESS == res_wait_for_fences);

    clearSwapChain();

    m_vulkan_context.createSwapchain();
    m_vulkan_context.createSwapchainImageViews();
    m_vulkan_context.createFramebufferImageAndView();

    m_main_camera_pass.updateAfterFramebufferRecreate();
    m_tone_mapping_pass.updateAfterFramebufferRecreate(m_main_camera_pass.getFramebufferImageViews()[_main_camera_pass_backup_buffer_odd]);
    m_color_grading_pass.updateAfterFramebufferRecreate(m_main_camera_pass.getFramebufferImageViews()[_main_camera_pass_backup_buffer_even]);
    PMotionBlurPass::setContext(helper_info);
    m_motion_blur_pass.updateAfterFramebufferRecreate(
        m_main_camera_pass.getFramebufferImageViews()[_main_camera_pass_backup_buffer_odd],
        m_global_render_resource._storage_buffer._global_upload_ringbuffer);
    m_combine_ui_pass.updateAfterFramebufferRecreate(m_main_camera_pass.getFramebufferImageViews()[_main_camera_pass_backup_buffer_odd], m_main_camera_pass.getFramebufferImageViews()[_main_camera_pass_backup_buffer_even]);
    m_mouse_pick_pass.recreateFramebuffer();

    return true;
}

void Pilot::PVulkanManager::clearSwapChain()
{
    vkDestroyImageView(m_vulkan_context._device, m_vulkan_context._depth_image_view, NULL);
    vkDestroyImage(m_vulkan_context._device, m_vulkan_context._depth_image, NULL);
    vkFreeMemory(m_vulkan_context._device, m_vulkan_context._depth_image_memory, NULL);

    m_vulkan_context.clearSwapchain();
}
