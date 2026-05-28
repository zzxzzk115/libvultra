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

#include <texture_headers/kenney_cursor-pack/PNG/Outline/Default/cursor_disabled.png.bintex.h>
#include <texture_headers/kenney_cursor-pack/PNG/Outline/Default/hand_open.png.bintex.h>
#include <texture_headers/kenney_cursor-pack/PNG/Outline/Default/pointer_a.png.bintex.h>
#include <texture_headers/kenney_cursor-pack/PNG/Outline/Default/pointer_i.png.bintex.h>
#include <texture_headers/kenney_cursor-pack/PNG/Outline/Default/resize_a_diagonal.png.bintex.h>
#include <texture_headers/kenney_cursor-pack/PNG/Outline/Default/resize_a_diagonal_mirror.png.bintex.h>
#include <texture_headers/kenney_cursor-pack/PNG/Outline/Default/resize_horizontal.png.bintex.h>
#include <texture_headers/kenney_cursor-pack/PNG/Outline/Default/resize_vertical.png.bintex.h>

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
    namespace
    {
        const os::Window::CursorImage* decodeImGuiCursor(std::span<const uint8_t> bytes, int hotX, int hotY)
        {
            static std::vector<std::optional<os::Window::CursorImage>> cache;
            static std::vector<const void*>                            keys;

            const void* key = bytes.data();
            for (std::size_t i = 0; i < keys.size(); ++i)
            {
                if (keys[i] == key)
                {
                    return cache[i] ? &(*cache[i]) : nullptr;
                }
            }

            keys.push_back(key);
            cache.push_back(os::Window::decodeCursorImage(bytes, hotX, hotY));
            return cache.back() ? &(*cache.back()) : nullptr;
        }

        os::Window::CursorType mapImGuiCursor(const ImGuiMouseCursor cursor)
        {
            switch (cursor)
            {
                case ImGuiMouseCursor_TextInput:
                    return os::Window::CursorType::eTextInput;
                case ImGuiMouseCursor_ResizeNS:
                    return os::Window::CursorType::eResizeNS;
                case ImGuiMouseCursor_ResizeEW:
                    return os::Window::CursorType::eResizeEW;
                case ImGuiMouseCursor_ResizeNESW:
                    return os::Window::CursorType::eResizeNESW;
                case ImGuiMouseCursor_ResizeNWSE:
                    return os::Window::CursorType::eResizeNWSE;
                case ImGuiMouseCursor_Hand:
                    return os::Window::CursorType::eHand;
                case ImGuiMouseCursor_NotAllowed:
                    return os::Window::CursorType::eNotAllowed;
                case ImGuiMouseCursor_None:
                case ImGuiMouseCursor_Arrow:
                default:
                    return os::Window::CursorType::eArrow;
            }
        }

        const os::Window::CursorImage* kennyImGuiCursor(const ImGuiMouseCursor cursor)
        {
            switch (cursor)
            {
                case ImGuiMouseCursor_TextInput:
                    return decodeImGuiCursor(pointer_i_png_bintex, 8, 8);
                case ImGuiMouseCursor_ResizeNS:
                    return decodeImGuiCursor(resize_horizontal_png_bintex, 8, 8);
                case ImGuiMouseCursor_ResizeEW:
                    return decodeImGuiCursor(resize_vertical_png_bintex, 8, 8);
                case ImGuiMouseCursor_ResizeNESW:
                    return decodeImGuiCursor(resize_a_diagonal_png_bintex, 8, 8);
                case ImGuiMouseCursor_ResizeNWSE:
                    return decodeImGuiCursor(resize_a_diagonal_mirror_png_bintex, 8, 8);
                case ImGuiMouseCursor_Hand:
                    return decodeImGuiCursor(hand_open_png_bintex, 8, 8);
                case ImGuiMouseCursor_NotAllowed:
                    return decodeImGuiCursor(cursor_disabled_png_bintex, 8, 8);
                case ImGuiMouseCursor_Arrow:
                default:
                    return decodeImGuiCursor(pointer_a_png_bintex, 8, 8);
            }
        }
    } // namespace

    VulkanImGui::VulkanImGui(const RenderDevice& renderDevice) : m_RenderDevice(renderDevice) {}

    VulkanImGui::~VulkanImGui() { shutdown({}, nullptr); }

    void VulkanImGui::init(const os::Window&   window,
                           const RenderDevice& renderDevice,
                           const Swapchain&    swapchain,
                           const bool /*enableMultiviewport*/,
                           const bool /*enableDocking*/)
    {
        m_Initialized = true;
        m_Window      = &window;

        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

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
        initInfo.MinImageCount       = static_cast<uint32_t>(swapchain.getNumBuffers());
        initInfo.ImageCount          = static_cast<uint32_t>(swapchain.getNumBuffers());
        initInfo.Allocator           = nullptr;
        initInfo.UseDynamicRendering = true;
#if IMGUI_VERSION_NUM >= 19250
        initInfo.PipelineInfoMain.Subpass     = 0;
        initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        initInfo.PipelineInfoMain.PipelineRenderingCreateInfo =
            static_cast<VkPipelineRenderingCreateInfo>(renderingCreateInfo);
#else
        initInfo.Subpass                     = 0;
        initInfo.MSAASamples                 = VK_SAMPLE_COUNT_1_BIT;
        initInfo.PipelineRenderingCreateInfo = static_cast<VkPipelineRenderingCreateInfo>(renderingCreateInfo);
#endif
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
        m_HasAppliedImGuiCursorOverride = false;
        m_Window                        = nullptr;
        m_Initialized                   = false;
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
        if (m_Window != nullptr)
        {
            ImGuiIO&   io = ImGui::GetIO();
            const bool uiOwnsCursor =
                io.WantCaptureMouse || ImGui::IsAnyItemHovered() || ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
            if (uiOwnsCursor)
            {
                auto& window = const_cast<os::Window&>(*m_Window);
                if (ImGui::GetMouseCursor() == ImGuiMouseCursor_None || io.MouseDrawCursor)
                {
                    window.setCursorVisibility(false);
                    if (m_HasAppliedImGuiCursorOverride)
                    {
                        window.clearCursorOverride();
                        m_HasAppliedImGuiCursorOverride = false;
                    }
                }
                else
                {
                    const ImGuiMouseCursor imguiCursor = ImGui::GetMouseCursor();
                    window.setCursorVisibility(true).setCursor(mapImGuiCursor(imguiCursor));
                    if (const auto* cursorImage = kennyImGuiCursor(imguiCursor); cursorImage != nullptr)
                    {
                        window.setCursorOverride(*cursorImage);
                        m_HasAppliedImGuiCursorOverride = true;
                    }
                    else if (m_HasAppliedImGuiCursorOverride)
                    {
                        window.clearCursorOverride();
                        m_HasAppliedImGuiCursorOverride = false;
                    }
                }
            }
            else if (m_HasAppliedImGuiCursorOverride)
            {
                auto& window = const_cast<os::Window&>(*m_Window);
                window.clearCursorOverride();
                m_HasAppliedImGuiCursorOverride = false;
            }
        }
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

    std::uintptr_t VulkanImGui::addTexture(const Texture& texture, const Sampler sampler)
    {
        auto* const descriptorSet =
            ImGui_ImplVulkan_AddTexture(asVkHandle<VkSampler>(m_RenderDevice.getSamplerHandle(sampler).value),
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
