#include "vultra/core/rhi/backends/webgpu/webgpu_imgui.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/event/window_events.hpp"
#include "vultra/core/rhi/backends/webgpu/conversions.hpp"
#include "vultra/core/rhi/backends/webgpu/webgpu_command_buffer_access.hpp"
#include "vultra/core/rhi/backends/webgpu/webgpu_render_device_access.hpp"
#include "vultra/core/rhi/render_device.hpp"
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

#if defined(__ANDROID__)
#include "vultra/platform/android/android_native_window.hpp"
#else
#include "vultra/platform/glfw/glfw_window.hpp"
#endif

#include <algorithm>
#include <imgui.h>
#include <stdexcept>

#if defined(VULTRA_ENABLE_WEBGPU) && VULTRA_ENABLE_WEBGPU && __has_include(<imgui_impl_wgpu.h>)
#define VULTRA_HAS_IMGUI_IMPL_WGPU 1
#include <imgui_impl_wgpu.h>
#else
#define VULTRA_HAS_IMGUI_IMPL_WGPU 0
#endif

#if defined(__ANDROID__)
#include <android/input.h>
#include <imgui_impl_android.h>
#else
#include <imgui_impl_glfw.h>
#endif

namespace vultra::rhi
{
    namespace
    {
        const os::Window::CursorImage* decodeImGuiCursor(std::span<const uint8_t> bytes, int hotX, int hotY)
        {
            static std::vector<std::optional<os::Window::CursorImage>> cache;
            static std::vector<const void*> keys;

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

    WebGPUImGui::WebGPUImGui(const RenderDevice& renderDevice) : m_RenderDevice(renderDevice) {}

    WebGPUImGui::~WebGPUImGui() { shutdown({}, nullptr); }

    void WebGPUImGui::init(const os::Window&   window,
                           const RenderDevice& renderDevice,
                           const Swapchain&    swapchain,
                           const bool          enableMultiviewport,
                           const bool          enableDocking)
    {
        m_Initialized = true;
        m_Window      = &window;

        (void)enableDocking;

#ifdef IMGUI_HAS_VIEWPORT
        if (enableMultiviewport && (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable))
        {
            ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
            if (!m_WarnedViewportUnsupported)
            {
                VULTRA_CORE_WARN("[WebGPUImGui] Multi-viewport is requested but not supported by imgui_impl_wgpu. "
                                 "Falling back to single viewport.");
                m_WarnedViewportUnsupported = true;
            }
        }
#else
        (void)enableMultiviewport;
#endif

#if defined(__ANDROID__)
        const auto& androidWindow = static_cast<const platform::android::AndroidNativeWindow&>(window);
        ImGui_ImplAndroid_Init(androidWindow.nativeWindow());
#else
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        const auto& glfwWindow = static_cast<const platform::glfw::GLFWWindow&>(window);
        const bool glfwOk = ImGui_ImplGlfw_InitForOther(glfwWindow.getHandle(), true);
        if (!glfwOk)
        {
            throw std::runtime_error("ImGui_ImplGlfw_InitForOther failed");
        }
#endif

#if VULTRA_HAS_IMGUI_IMPL_WGPU
        ImGui_ImplWGPU_InitInfo initInfo {};
        initInfo.Device = reinterpret_cast<WGPUDevice>(WebGPURenderDeviceAccess::getDeviceHandle(renderDevice));
        initInfo.NumFramesInFlight  = static_cast<int>(std::max<std::size_t>(swapchain.getNumBuffers(), 2));
        initInfo.RenderTargetFormat = webgpu::toWgpuTextureFormat(swapchain.getPixelFormat());
        const bool wgpuOk = ImGui_ImplWGPU_Init(&initInfo);
        if (!wgpuOk)
        {
            throw std::runtime_error("ImGui_ImplWGPU_Init failed");
        }
#else
        (void)renderDevice;
        (void)swapchain;
        if (!m_WarnedTexturePath)
        {
            VULTRA_CORE_ERROR("[WebGPUImGui] imgui_impl_wgpu.h is unavailable for this build; "
                              "WebGPU ImGui rendering is disabled.");
            m_WarnedTexturePath = true;
        }
#endif
    }

    void WebGPUImGui::shutdown(const std::string&, const char*)
    {
        if (!m_Initialized)
            return;

#if VULTRA_HAS_IMGUI_IMPL_WGPU
        ImGui_ImplWGPU_Shutdown();
#endif
#if defined(__ANDROID__)
        ImGui_ImplAndroid_Shutdown();
#else
        ImGui_ImplGlfw_Shutdown();
#endif
        m_HasAppliedImGuiCursorOverride = false;
        m_Window      = nullptr;
        m_Initialized = false;
    }

    void WebGPUImGui::beginFrame(const os::Window& window)
    {
#if VULTRA_HAS_IMGUI_IMPL_WGPU
        ImGui_ImplWGPU_NewFrame();
#endif
#if defined(__ANDROID__)
        ImGui_ImplAndroid_NewFrame();
#else
        (void)window;
        ImGui_ImplGlfw_NewFrame();
#endif
    }

    void WebGPUImGui::render(CommandBuffer& cb)
    {
#if VULTRA_HAS_IMGUI_IMPL_WGPU
        ImGui::Render();
        ImDrawData* drawData = ImGui::GetDrawData();
        auto* const renderPass = WebGPUCommandBufferAccess::getCurrentRenderPassEncoder(cb);
        if (renderPass != nullptr)
        {
            ImGui_ImplWGPU_RenderDrawData(drawData, renderPass);
        }
#else
        (void)cb;
#endif
    }

    void WebGPUImGui::postRender()
    {
        if (m_Window != nullptr)
        {
            ImGuiIO& io = ImGui::GetIO();
            const bool uiOwnsCursor = io.WantCaptureMouse || ImGui::IsAnyItemHovered() ||
                                      ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
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
        // imgui_impl_wgpu currently has no multi-viewport renderer support.
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable && !m_WarnedViewportUnsupported)
        {
            VULTRA_CORE_WARN(
                "[WebGPUImGui] Multi-viewport flag is still set, but WebGPU backend cannot render platform windows.");
            m_WarnedViewportUnsupported = true;
        }
#endif
    }

    void WebGPUImGui::processEvent(const os::GeneralWindowEvent& event)
    {
#if defined(__ANDROID__)
        if (event.nativeEventSource == vultra::event::NativeEventSource::eAndroidInput && event.nativeEvent != nullptr)
        {
            ImGui_ImplAndroid_HandleInputEvent(reinterpret_cast<AInputEvent*>(const_cast<void*>(event.nativeEvent)));
        }
#else
        (void)event;
#endif
    }

    std::uintptr_t WebGPUImGui::addTexture(const Texture& texture, const Sampler sampler)
    {
        if (!m_Initialized)
            return 0;

#if VULTRA_HAS_IMGUI_IMPL_WGPU
        (void)sampler;
        return texture.getImageView().getHandle();
#else
        if (!m_WarnedTexturePath)
        {
            VULTRA_CORE_WARN("[WebGPUImGui] imgui_impl_wgpu.h is unavailable; addTexture returns empty id.");
            m_WarnedTexturePath = true;
        }
        (void)texture;
        (void)sampler;
        return 0;
#endif
    }

    void WebGPUImGui::removeTexture(std::uintptr_t& textureId) { textureId = 0; }
} // namespace vultra::rhi
