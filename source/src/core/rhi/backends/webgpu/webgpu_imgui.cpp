#include "vultra/core/rhi/backends/webgpu/webgpu_imgui.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/event/window_events.hpp"
#include "vultra/core/rhi/backends/webgpu/conversions.hpp"
#include "vultra/core/rhi/backends/webgpu/webgpu_command_buffer_access.hpp"
#include "vultra/core/rhi/backends/webgpu/webgpu_render_device_access.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/swapchain.hpp"
#include "vultra/core/rhi/texture.hpp"

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
    WebGPUImGui::WebGPUImGui(const RenderDevice& renderDevice) : m_RenderDevice(renderDevice) {}

    WebGPUImGui::~WebGPUImGui() { shutdown({}, nullptr); }

    void WebGPUImGui::init(const os::Window&   window,
                           const RenderDevice& renderDevice,
                           const Swapchain&    swapchain,
                           const bool          enableMultiviewport,
                           const bool          enableDocking)
    {
        m_Initialized = true;

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

    std::uintptr_t WebGPUImGui::addTexture(const Texture& texture)
    {
        if (!m_Initialized)
            return 0;

#if VULTRA_HAS_IMGUI_IMPL_WGPU
        return texture.getImageView().getHandle();
#else
        if (!m_WarnedTexturePath)
        {
            VULTRA_CORE_WARN("[WebGPUImGui] imgui_impl_wgpu.h is unavailable; addTexture returns empty id.");
            m_WarnedTexturePath = true;
        }
        (void)texture;
        return 0;
#endif
    }

    void WebGPUImGui::removeTexture(std::uintptr_t& textureId) { textureId = 0; }
} // namespace vultra::rhi
