#pragma once

#include "vultra/core/app/app_host.hpp"
#include "vultra/core/base/base.hpp"
#include "vultra/core/os/window.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/function/camera/camera_system.hpp"
#include "vultra/function/rendering/srp/renderer.hpp"

#include <optional>
#include <string_view>

#if defined(__ANDROID__)
#include "vultra/platform/android/android_app_runtime_context.hpp"
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

        virtual std::string_view                 demoWindowTitle() const { return "Vultra Demo App"; }
        virtual bool                             demoWindowResizable() const { return true; }
        virtual rhi::RenderDeviceFeatureFlagBits demoRenderDeviceFeatureFlag() const
        {
            return rhi::RenderDeviceFeatureFlagBits::eNormal;
        }

        virtual FPSCameraController makeFPSCameraController() const;
        virtual Ref<Renderer>       makeRenderer() const;

        virtual void onWindowEvent(const os::GeneralWindowEvent& e);

        virtual void onConfigureDemo(Engine& /*engine*/) {}
        virtual void onPostConfigureDemo(Engine& /*engine*/) {}

#if defined(__ANDROID__)
    private:
        std::optional<platform::android::AndroidAppRuntimeContext> m_AndroidRuntimeContext;
#endif
    };
} // namespace vultra
