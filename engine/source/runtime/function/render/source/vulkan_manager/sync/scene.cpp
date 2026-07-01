#include "runtime/function/render/include/render/glm_wrapper.h"
#include "runtime/function/render/include/render/render.h"
#include "runtime/function/render/include/render/vulkan_manager/vulkan_manager.h"

void Pilot::PVulkanManager::cullingAndSyncScene(class Scene&                scene,
                                                class PilotRenderer*        pilot_renderer,
                                                struct SceneReleaseHandles& release_handles)
{
    // set perframe ubo data
    glm::mat4 proj_view_matrix;
    {
        PCamera*  camera          = scene.m_camera.get();
        Matrix4x4 view_matrix     = camera->getViewMatrix();
        Matrix4x4 proj_matrix     = camera->getPersProjMatrix();
        Vector3   camera_position = camera->position();
        proj_view_matrix          = GLMUtil::fromMat4x4(proj_matrix * view_matrix);

        // ambient light
        Vector3  ambient_light   = scene.m_ambient_light.m_irradiance;
        uint32_t point_light_num = static_cast<uint32_t>(scene.m_pointLights.m_lights.size());

        // set ubo data
        m_mesh_perframe_storage_buffer_object.proj_view_matrix = proj_view_matrix;
        m_mesh_perframe_storage_buffer_object.camera_position  = GLMUtil::fromVec3(camera_position);
        m_mesh_perframe_storage_buffer_object.ambient_light    = ambient_light;
        m_mesh_perframe_storage_buffer_object.point_light_num  = point_light_num;

        m_mesh_point_light_shadow_perframe_storage_buffer_object.point_light_num = point_light_num;

        // point lights
        for (uint32_t i = 0; i < point_light_num; i++)
        {
            Vector3 point_light_position  = scene.m_pointLights.m_lights[i].m_position;
            Vector3 point_light_intensity = scene.m_pointLights.m_lights[i].m_flux / (4.0f * glm::pi<float>());

            float radius = scene.m_pointLights.m_lights[i].calculateRadius();

            m_mesh_perframe_storage_buffer_object.scene_point_lights[i].position  = point_light_position;
            m_mesh_perframe_storage_buffer_object.scene_point_lights[i].radius    = radius;
            m_mesh_perframe_storage_buffer_object.scene_point_lights[i].intensity = point_light_intensity;

            m_mesh_point_light_shadow_perframe_storage_buffer_object.point_lights_position_and_radius[i] =
                Vector4(point_light_position, radius);
        }

        // directional light
        m_mesh_perframe_storage_buffer_object.scene_directional_light.direction =
            scene.m_directional_light.m_direction.normalisedCopy();
        m_mesh_perframe_storage_buffer_object.scene_directional_light.color = scene.m_directional_light.m_color;

        m_mesh_inefficient_pick_perframe_storage_buffer_object.proj_view_matrix = proj_view_matrix;
        m_mesh_inefficient_pick_perframe_storage_buffer_object.rt_width  = m_vulkan_context._swapchain_extent.width;
        m_mesh_inefficient_pick_perframe_storage_buffer_object.rt_height = m_vulkan_context._swapchain_extent.height;

        m_particlebillboard_perframe_storage_buffer_object.proj_view_matrix = proj_view_matrix;
        m_particlebillboard_perframe_storage_buffer_object.eye_position     = GLMUtil::fromVec3(camera_position);
        m_particlebillboard_perframe_storage_buffer_object.up_direction     = GLMUtil::fromVec3(camera->up());
    }

    culling(scene, pilot_renderer, release_handles);

    glm::mat4 inv_current_proj_view = glm::inverse(proj_view_matrix);
    uint32_t motion_blur_dynamic_offset = roundUp(
        m_global_render_resource._storage_buffer._global_upload_ringbuffers_end[m_current_frame_index],
        m_global_render_resource._storage_buffer._min_uniform_buffer_offset_alignment);
    m_global_render_resource._storage_buffer._global_upload_ringbuffers_end[m_current_frame_index] =
        motion_blur_dynamic_offset + sizeof(MotionBlurUBO);
    assert(m_global_render_resource._storage_buffer._global_upload_ringbuffers_end[m_current_frame_index] <=
        (m_global_render_resource._storage_buffer._global_upload_ringbuffers_begin[m_current_frame_index] +
         m_global_render_resource._storage_buffer._global_upload_ringbuffers_size[m_current_frame_index]));
    MotionBlurUBO* dst_motion_blur_ubo_ptr = reinterpret_cast<MotionBlurUBO*>(
        reinterpret_cast<uintptr_t>(m_global_render_resource._storage_buffer._global_upload_ringbuffer_memory_pointer) +
        motion_blur_dynamic_offset);
    dst_motion_blur_ubo_ptr->inv_CurrentVP = inv_current_proj_view;
    dst_motion_blur_ubo_ptr->prev_VP       = m_prev_proj_view_matrix;
    dst_motion_blur_ubo_ptr->blurScale     = 1.0f;
    m_motion_blur_pass.setDynamicOffset(motion_blur_dynamic_offset);
    m_prev_proj_view_matrix = proj_view_matrix;

    // set perframe ubo data
    {
        m_main_camera_pass.m_is_show_axis                        = m_is_show_axis;
        m_main_camera_pass.m_selected_axis                       = m_selected_axis;
        m_main_camera_pass.m_mesh_perframe_storage_buffer_object = m_mesh_perframe_storage_buffer_object;
        m_main_camera_pass.m_axis_storage_buffer_object          = m_axis_storage_buffer_object;
        m_main_camera_pass.m_particlebillboard_perframe_storage_buffer_object =
            m_particlebillboard_perframe_storage_buffer_object;

        m_directional_light_shadow_pass._mesh_directional_light_shadow_perframe_storage_buffer_object =
            m_mesh_directional_light_shadow_perframe_storage_buffer_object;

        m_point_light_shadow_pass._mesh_point_light_shadow_perframe_storage_buffer_object =
            m_mesh_point_light_shadow_perframe_storage_buffer_object;

        m_mouse_pick_pass._mesh_inefficient_pick_perframe_storage_buffer_object =
            m_mesh_inefficient_pick_perframe_storage_buffer_object;
    }
}
