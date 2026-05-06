#pragma once

#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/os/window.hpp"

// NOLINTBEGIN
#include <imgui.h>
// NOLINTEND

#include <vbase/service/service_registry.hpp>

namespace vultra
{
    class IImGuiService
    {
    public:
        SERVICE_REGISTER(IImGuiService);

        using TextureID = ImTextureID;

        virtual void      begin()                                                          = 0;
        virtual void      render(rhi::CommandBuffer& cb, const rhi::FramebufferInfo& info) = 0;
        virtual void      end()                                                            = 0;
        virtual void      postRender()                                                     = 0;
        virtual void      processEvent(const os::GeneralWindowEvent& event)               = 0;
        virtual TextureID addTexture(const rhi::Texture& texture)                          = 0;
        virtual void      removeTexture(TextureID& textureID)                              = 0;
    };
} // namespace vultra
