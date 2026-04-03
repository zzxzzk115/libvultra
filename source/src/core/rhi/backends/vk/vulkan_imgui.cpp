#include "vultra/core/rhi/backends/vk/vulkan_imgui.hpp"

#include "vultra/core/event/window_events.hpp"
#include "vultra/core/rhi/backends/vk/conversions.hpp"
#include "vultra/core/rhi/backends/vk/handle_utils.hpp"
#include "vultra/core/rhi/backends/vk/vulkan_render_device_access.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/structs/pixel_format.hpp"
#include "vultra/core/rhi/swapchain.hpp"
#include "vultra/core/rhi/texture.hpp"

#include "vultra/platform/android/android_native_window.hpp"
#include "vultra/platform/sdl/sdl_window.hpp"

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#if defined(__ANDROID__)
#include <android/input.h>
#include <imgui_impl_android.h>
#else
#include <SDL3/SDL_video.h>
#include <imgui_impl_sdl3.h>
#endif

namespace vultra::rhi
{
    VulkanImGui::VulkanImGui(const RenderDevice& renderDevice) : m_RenderDevice(renderDevice) {}

    VulkanImGui::~VulkanImGui() { shutdown({}, nullptr); }

    void VulkanImGui::init(const os::Window&   window,
                           const RenderDevice& renderDevice,
                           const Swapchain&    swapchain,
                           const bool /*enableMultiviewport*/,
                           const bool /*enableDocking*/)
    {
        m_Initialized = true;

#if defined(__ANDROID__)
        const auto& androidWindow = static_cast<const platform::android::AndroidNativeWindow&>(window);
        ImGui_ImplAndroid_Init(androidWindow.nativeWindow());
#else
        const auto& sdlWindow = static_cast<const platform::sdl::SDLWindow&>(window);
        ImGui_ImplSDL3_InitForVulkan(sdlWindow.getHandle());
#endif

        static vk::Format               colorFormat = toVk(swapchain.getPixelFormat());
        vk::PipelineRenderingCreateInfo renderingCreateInfo {};
        renderingCreateInfo.setColorAttachmentFormats(colorFormat);

        ImGui_ImplVulkan_InitInfo initInfo {};
        initInfo.Instance = asVkHandle<VkInstance>(VulkanRenderDeviceAccess::getInstanceHandle(renderDevice));
        initInfo.PhysicalDevice =
            asVkHandle<VkPhysicalDevice>(VulkanRenderDeviceAccess::getPhysicalDeviceHandle(renderDevice));
        initInfo.Device        = asVkHandle<VkDevice>(VulkanRenderDeviceAccess::getDeviceHandle(renderDevice));
        initInfo.QueueFamily   = VulkanRenderDeviceAccess::getQueueFamilyIndex(renderDevice);
        initInfo.Queue         = asVkHandle<VkQueue>(VulkanRenderDeviceAccess::getQueueHandle(renderDevice));
        initInfo.PipelineCache = VK_NULL_HANDLE;
        initInfo.DescriptorPool =
            asVkHandle<VkDescriptorPool>(VulkanRenderDeviceAccess::getDescriptorPoolHandle(renderDevice));
        initInfo.Subpass                     = 0;
        initInfo.MinImageCount               = static_cast<uint32_t>(swapchain.getNumBuffers());
        initInfo.ImageCount                  = static_cast<uint32_t>(swapchain.getNumBuffers());
        initInfo.MSAASamples                 = VK_SAMPLE_COUNT_1_BIT;
        initInfo.Allocator                   = nullptr;
        initInfo.UseDynamicRendering         = true;
        initInfo.PipelineRenderingCreateInfo = static_cast<VkPipelineRenderingCreateInfo>(renderingCreateInfo);
        ImGui_ImplVulkan_Init(&initInfo);
    }

    void VulkanImGui::shutdown(const std::string&, const char*)
    {
        if (!m_Initialized)
            return;

        ImGui_ImplVulkan_Shutdown();
#if defined(__ANDROID__)
        ImGui_ImplAndroid_Shutdown();
#else
        ImGui_ImplSDL3_Shutdown();
#endif
        m_Initialized = false;
    }

    void VulkanImGui::beginFrame(const os::Window& window)
    {
        ImGui_ImplVulkan_NewFrame();
#if defined(__ANDROID__)
        ImGui_ImplAndroid_NewFrame();
#else
        (void)window;
        ImGui_ImplSDL3_NewFrame();
#endif
    }

    void VulkanImGui::render(CommandBuffer& cb)
    {
        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), asVkHandle<VkCommandBuffer>(cb.getHandle()));
    }

    void VulkanImGui::postRender()
    {
#ifdef IMGUI_HAS_VIEWPORT
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            if (ImGui::GetDrawData() != nullptr)
            {
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
            }
        }
#endif
    }

    void VulkanImGui::processEvent(const os::GeneralWindowEvent& event)
    {
#if defined(__ANDROID__)
        if (event.nativeEventSource == vultra::event::NativeEventSource::eAndroidInput && event.nativeEvent != nullptr)
        {
            ImGui_ImplAndroid_HandleInputEvent(reinterpret_cast<AInputEvent*>(const_cast<void*>(event.nativeEvent)));
        }
#else
        if (event.nativeEventSource == vultra::event::NativeEventSource::eSDL3 && event.nativeEvent != nullptr)
        {
            ImGui_ImplSDL3_ProcessEvent(reinterpret_cast<SDL_Event*>(const_cast<void*>(event.nativeEvent)));
        }
#endif
    }

    std::uintptr_t VulkanImGui::addTexture(const Texture& texture)
    {
        auto* const descriptorSet = ImGui_ImplVulkan_AddTexture(
            asVkHandle<VkSampler>(m_RenderDevice.getSamplerHandle(texture.getSampler()).value),
            asVkHandle<VkImageView>(texture.getImageView().getHandle()),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        return toBackendHandle(descriptorSet);
    }

    void VulkanImGui::removeTexture(std::uintptr_t& textureId)
    {
        if (!textureId)
            return;

        ImGui_ImplVulkan_RemoveTexture(asVkHandle<VkDescriptorSet>(textureId));
        textureId = 0;
    }
} // namespace vultra::rhi
