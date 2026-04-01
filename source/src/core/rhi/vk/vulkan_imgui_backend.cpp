#include "vultra/core/rhi/vk/vulkan_imgui_backend.hpp"

#include "vultra/core/event/window_events.hpp"
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
    VulkanImGuiBackend::VulkanImGuiBackend(const RenderDevice& renderDevice) : m_RenderDevice(renderDevice) {}

    VulkanImGuiBackend::~VulkanImGuiBackend() { shutdown({}, nullptr); }

    void VulkanImGuiBackend::init(const os::Window&      window,
                                  const RenderDevice&    renderDevice,
                                  const Swapchain&       swapchain,
                                  const bool             enableMultiviewport,
                                  const bool             enableDocking)
    {
        m_Initialized = true;

#if defined(__ANDROID__)
        const auto& androidWindow = static_cast<const platform::android::AndroidNativeWindow&>(window);
        ImGui_ImplAndroid_Init(androidWindow.nativeWindow());
#else
        const auto& sdlWindow = static_cast<const platform::sdl::SDLWindow&>(window);
        ImGui_ImplSDL3_InitForVulkan(sdlWindow.getHandle());
#endif

        static vk::Format colorFormat = toVk(swapchain.getPixelFormat());
        vk::PipelineRenderingCreateInfo renderingCreateInfo {};
        renderingCreateInfo.setColorAttachmentFormats(colorFormat);

        ImGui_ImplVulkan_InitInfo initInfo {};
        initInfo.Instance                    = reinterpret_cast<VkInstance>(renderDevice.getNativeInstanceHandle());
        initInfo.PhysicalDevice              = reinterpret_cast<VkPhysicalDevice>(renderDevice.getNativePhysicalDeviceHandle());
        initInfo.Device                      = reinterpret_cast<VkDevice>(renderDevice.getNativeDeviceHandle());
        initInfo.QueueFamily                 = renderDevice.getNativeQueueFamilyIndex();
        initInfo.Queue                       = reinterpret_cast<VkQueue>(renderDevice.getNativeQueueHandle());
        initInfo.PipelineCache               = VK_NULL_HANDLE;
        initInfo.DescriptorPool              = reinterpret_cast<VkDescriptorPool>(renderDevice.getNativeDescriptorPoolHandle());
        initInfo.Subpass                     = 0;
        initInfo.MinImageCount               = static_cast<uint32_t>(swapchain.getNumBuffers());
        initInfo.ImageCount                  = static_cast<uint32_t>(swapchain.getNumBuffers());
        initInfo.MSAASamples                 = VK_SAMPLE_COUNT_1_BIT;
        initInfo.Allocator                   = nullptr;
        initInfo.UseDynamicRendering         = true;
        initInfo.PipelineRenderingCreateInfo = static_cast<VkPipelineRenderingCreateInfo>(renderingCreateInfo);
        ImGui_ImplVulkan_Init(&initInfo);
    }

    void VulkanImGuiBackend::shutdown(const std::string&, const char*)
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

    void VulkanImGuiBackend::beginFrame(const os::Window& window)
    {
        ImGui_ImplVulkan_NewFrame();
#if defined(__ANDROID__)
        ImGui_ImplAndroid_NewFrame();
#else
        (void)window;
        ImGui_ImplSDL3_NewFrame();
#endif
    }

    void VulkanImGuiBackend::render(CommandBuffer& cb)
    {
        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cb.getHandle());
    }

    void VulkanImGuiBackend::postRender()
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

    void VulkanImGuiBackend::processEvent(const os::GeneralWindowEvent& event)
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

    std::uintptr_t VulkanImGuiBackend::addTexture(const Texture& texture)
    {
        return reinterpret_cast<std::uintptr_t>(ImGui_ImplVulkan_AddTexture(m_RenderDevice.getSamplerHandle(texture.getSampler()),
                                                                            reinterpret_cast<VkImageView>(texture.getImageView().getNativeHandle()),
                                                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
    }

    void VulkanImGuiBackend::removeTexture(std::uintptr_t& textureId)
    {
        if (!textureId)
            return;

        ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(textureId));
        textureId = 0;
    }
} // namespace vultra::rhi
