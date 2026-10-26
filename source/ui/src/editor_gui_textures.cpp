#include <vultra/ui/editor_gui.hpp>

#include <algorithm>
#include <cstring>
#include <numeric>
#include <stdexcept>

namespace vultra
{
    void EditorGui::updateTextures()
    {
        // ImGui 1.92 bakes glyphs at the requested size/density, including detached viewport DPI.
        // The renderer owns these resources; user-provided image views are never added to this map.
        for (auto* data : ImGui::GetPlatformIO().Textures)
        {
            if (data->Status == ImTextureStatus_OK || data->Status == ImTextureStatus_Destroyed)
            {
                continue;
            }
            auto* view = reinterpret_cast<VriDescriptor*>(data->GetTexID());
            if (data->Status == ImTextureStatus_WantDestroy)
            {
                m_Device.waitIdle();
                m_Api.FreeImguiTexture(m_Renderer, view);
                m_Textures.erase(view);
                data->SetTexID(ImTextureID_Invalid);
                data->SetStatus(ImTextureStatus_Destroyed);
                continue;
            }
            const bool create = data->Status == ImTextureStatus_WantCreate;
            if (data->Format != ImTextureFormat_RGBA32)
            {
                throw std::runtime_error("ImGui atlas requires RGBA32 pixels");
            }
            if (create)
            {
                auto desc    = colorTexture({uint32_t(data->Width), uint32_t(data->Height)});
                desc.usage   = VriTextureUsage_ShaderResource | VriTextureUsage_TransferDst;
                auto texture = std::make_unique<Texture>(m_Device, desc);
                view         = texture->view();
                m_Textures.emplace(view, std::move(texture));
                data->SetTexID(reinterpret_cast<ImTextureID>(view));
            }
            auto&          texture = *m_Textures.at(view);
            const uint32_t x       = create ? 0 : data->UpdateRect.x;
            const uint32_t y       = create ? 0 : data->UpdateRect.y;
            const uint32_t width   = create ? data->Width : data->UpdateRect.w;
            const uint32_t height  = create ? data->Height : data->UpdateRect.h;
            const auto     alignment =
                std::lcm(256u,
                         std::max(1u, m_Device.core.GetDeviceDesc(m_Device.handle)->uploadBufferTextureRowAlignment));
            const auto     pitch = (width * 4 + alignment - 1) / alignment * alignment;
            const uint64_t bytes = uint64_t(pitch) * height;
            Buffer         staging(m_Device, {bytes, 0, VriBufferUsage_TransferSrc, VriMemoryLocation_HostUpload});
            auto*          mapped = static_cast<std::byte*>(m_Device.core.MapBuffer(staging.handle, 0, bytes));
            if (!mapped)
            {
                throw std::runtime_error("Map ImGui atlas upload buffer");
            }
            for (uint32_t row = 0; row < height; ++row)
            {
                std::memcpy(mapped + uint64_t(row) * pitch, data->GetPixelsAt(int(x), int(y + row)), width * 4);
            }
            m_Device.core.UnmapBuffer(staging.handle);
            Frame frame(m_Device);
            auto* commands = frame.begin();
            texture.transition(commands,
                               {VriAccess_CopyDestinationWrite, VriLayout_CopyDestination, VriPipelineStage_Transfer});
            VriBufferTextureCopyDesc copy {};
            copy.bufferRowLength   = pitch / 4;
            copy.bufferImageHeight = height;
            copy.texture           = {0, 0, 1, VriImageAspect_Color, int32_t(x), int32_t(y), 0, width, height, 1};
            m_Device.core.CmdUploadBufferToTexture(commands, texture.handle, staging.handle, &copy);
            texture.transition(commands,
                               {VriAccess_ShaderResourceRead, VriLayout_ShaderResource, VriPipelineStage_AllCommands});
            // Atlas changes are infrequent. Complete the upload before releasing staging memory or drawing any
            // viewport.
            frame.submitAndWait();
            data->SetStatus(ImTextureStatus_OK);
        }
    }

    void EditorGui::destroyTextures()
    {
        for (auto* data : ImGui::GetPlatformIO().Textures)
        {
            auto* view = reinterpret_cast<VriDescriptor*>(data->GetTexID());
            if (m_Textures.contains(view))
            {
                m_Api.FreeImguiTexture(m_Renderer, view);
                data->SetTexID(ImTextureID_Invalid);
                data->SetStatus(ImTextureStatus_Destroyed);
            }
        }
        m_Textures.clear();
    }
} // namespace vultra
