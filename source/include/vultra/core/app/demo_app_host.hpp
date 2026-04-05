#pragma once

#include "vultra/core/app/app_host.hpp"
#include "vultra/core/base/base.hpp"
#include "vultra/core/os/window.hpp"
#include "vultra/function/camera/camera_system.hpp"
#include "vultra/function/rendering/srp/renderer.hpp"

#include <string_view>

#if defined(__ANDROID__)
#include "vultra/platform/android/android_app_runtime_context.hpp"
#include <optional>
#endif

namespace vultra
{
    class VULTRA_API DemoAppHost : public AppHost
    {
    public:
#if defined(__ANDROID__)
        void setAndroidRuntimeContext(const platform::android::AndroidAppRuntimeContext& runtimeContext);
#endif

    protected:
        void onConfigure(Engine& engine) override final;
        void onPostConfigure(Engine& engine) override final;

        void onPollEvents() override;
        bool onShouldClose() const override;

        virtual std::string_view      demoWindowTitle() const { return "libvultra Demo App"; }
        virtual bool                  demoWindowResizable() const { return true; }
        virtual rhi::RenderBackendApi demoRenderBackendApi() const
        {
#if defined(__EMSCRIPTEN__)
            return rhi::RenderBackendApi::eWebGPU;
#else
            return rhi::RenderBackendApi::eVulkan;
#endif
        }
        virtual bool                             demoAllowCliBackendOverride() const { return true; }
        virtual rhi::RenderDeviceFeatureFlagBits demoRenderDeviceFeatureFlag() const
        {
            return rhi::RenderDeviceFeatureFlagBits::eNormal;
        }

        virtual FPSCameraController makeFPSCameraController() const;
        virtual Ref<Renderer>       makeRenderer() const;

        virtual void onWindowEvent(const os::GeneralWindowEvent& e);

        // WebGPU path is still under active bring-up. Demo apps can opt-in to full scene/render content explicitly.
        virtual bool demoEnableExperimentalWebGPUContent() const { return false; }

        virtual void onConfigureDemo(Engine& /*engine*/) {}
        virtual void onPostConfigureDemo(Engine& /*engine*/) {}

#if defined(__ANDROID__)
    private:
        std::optional<platform::android::AndroidAppRuntimeContext> m_AndroidRuntimeContext;
#endif
    };
} // namespace vultra
