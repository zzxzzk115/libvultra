#pragma once

#include "sample.hpp"

#include <vultra/core/profiling/profiler.hpp>
#include <vultra/function/openxr/openxr.hpp>
#include <vultra/function/renderer/gui.hpp>
#include <vultra/function/renderer/texture_blit.hpp>

namespace sample
{
    // The two XR examples share acquisition, submission and mirror ownership.
    // Their eye rendering stays explicit in each example.
    class XrSample : public vultra::BaseApp
    {
    public:
        XrSample(const Options& options, const std::string& title);
        void run(uint64_t frameLimit = 0) final;

    protected:
        vultra::Device& device()
        {
            return m_Device;
        }

        vultra::Window& window()
        {
            return m_Window;
        }

        vultra::Gui& gui()
        {
            return m_Gui;
        }

        VriFormat eyeFormat() const
        {
            return m_Session.format();
        }

        virtual void onImGui();
        virtual void onPreRender();
        virtual void onRenderEye(VriCommandBuffer* cmd, const vultra::XREye& eye, uint32_t index) = 0;

    private:
        Options               m_Options;
        vultra::Window        m_Window;
        vultra::OpenXRSystem  m_System;
        vultra::Device        m_Device;
        vultra::OpenXRSession m_Session;
        vultra::Swapchain     m_Desktop;
        vultra::Frame         m_Commands;
        vultra::TextureBlit   m_Mirror;
        vultra::Gui           m_Gui;
        vultra::Profiler      m_Profiler;
        vultra::Profiler      m_FrameProfiler;
    };
} // namespace sample
