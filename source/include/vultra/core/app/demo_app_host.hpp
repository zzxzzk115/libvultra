#pragma once

#include "vultra/core/app/app_host.hpp"
#include "vultra/core/os/window.hpp"
#include "vultra/function/camera/camera_system.hpp"

#include <string_view>

namespace vultra
{
    class VULTRA_API DemoAppHost : public AppHost
    {
    protected:
        void onConfigure(Engine& engine) override final;
        void onPostConfigure(Engine& engine) override final;

        void onPollEvents() override;
        bool onShouldClose() const override;

        virtual std::string_view    demoWindowTitle() const { return "Vultra Demo App"; }
        virtual bool                demoWindowResizable() const { return false; }
        virtual FPSCameraController makeFPSCameraController() const;

        virtual void onWindowEvent(const os::GeneralWindowEvent& e);

        virtual void onConfigureDemo(Engine& /*engine*/) {}
        virtual void onPostConfigureDemo(Engine& /*engine*/) {}
    };
} // namespace vultra
