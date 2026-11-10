#pragma once

#include <vultra/servers/rendering/scene.hpp>

#include <cstring>

namespace vultra::test
{
    // A real external GPU write into the accepted vertex allocation. Fixtures keep all non-position bytes unchanged.
    inline std::expected<void, std::string> writeScenePositionsExpected(Device&                          device,
                                                                        GpuScene&                        scene,
                                                                        std::span<const SceneVertex>     vertices,
                                                                        std::span<const PrimitiveBounds> bounds)
    {
        const auto bytes = std::as_bytes(vertices);
        if (bytes.size() != scene.vertices->desc.size)
        {
            return std::unexpected("Position fixture requires the complete existing vertex allocation");
        }
        auto staging =
            Buffer::create(device, {bytes.size(), 0, VriBufferUsage_TransferSrc, VriMemoryLocation_HostUpload});
        auto frame = Frame::create(device);
        if (!staging || !frame)
        {
            return std::unexpected(staging ? frame.error() : staging.error());
        }
        auto* mapped = device.core.MapBuffer((*staging)->handle, 0, bytes.size());
        if (!mapped)
        {
            return std::unexpected("Map position fixture staging buffer failed");
        }
        std::memcpy(mapped, bytes.data(), bytes.size());
        device.core.UnmapBuffer((*staging)->handle);
        auto commands = (*frame)->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        if (auto ready =
                scene.vertices->transitionExpected(*commands,
                                                   {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
            !ready)
        {
            return ready;
        }
        VriBufferCopyDesc copy {};
        copy.size = bytes.size();
        device.core.CmdCopyBuffer(*commands, scene.vertices->handle, (*staging)->handle, &copy);
        if (auto completed = (*frame)->submitAndWaitExpected(); !completed)
        {
            return completed;
        }
        return scene.notifyVertexPositionsChangedExpected(bounds);
    }
} // namespace vultra::test
