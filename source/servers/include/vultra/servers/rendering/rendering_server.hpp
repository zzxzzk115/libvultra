#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace vultra
{
    class Device;
    class GpuScene;
    struct SceneData;
    struct ImportedAsset;
    class RenderingServer;

    struct GpuSceneRid
    {
        uint64_t    value                                = 0;
        friend bool operator==(GpuSceneRid, GpuSceneRid) = default;
    };

    // A move-only owner of a server resource. The server must outlive it; borrowed scenes expire when it releases.
    class GpuSceneHandle
    {
    public:
        GpuSceneHandle() = default;
        ~GpuSceneHandle();
        GpuSceneHandle(GpuSceneHandle&& other) noexcept;
        GpuSceneHandle& operator=(GpuSceneHandle&& other) noexcept;
        GpuSceneHandle(const GpuSceneHandle&)            = delete;
        GpuSceneHandle& operator=(const GpuSceneHandle&) = delete;

        GpuScene&   get() const;
        GpuScene&   operator*() const;
        GpuScene*   operator->() const;
        GpuSceneRid rid() const;

    private:
        friend class RenderingServer;
        GpuSceneHandle(RenderingServer& server, GpuSceneRid rid);
        void reset() noexcept;

        RenderingServer* m_Server = nullptr;
        GpuSceneRid      m_Rid;
    };

    // Owns the GPU scenes created through this server. Direct GpuScene construction remains available.
    class RenderingServer
    {
    public:
        explicit RenderingServer(Device& device);
        ~RenderingServer();
        RenderingServer(const RenderingServer&)            = delete;
        RenderingServer& operator=(const RenderingServer&) = delete;

        GpuSceneHandle uploadScene(const SceneData& scene, bool meshShading = false, uint32_t workers = 0);
        GpuSceneHandle uploadScene(const ImportedAsset& asset, bool meshShading = false, uint32_t workers = 0);
        GpuScene&      scene(GpuSceneRid rid) const;
        bool           release(GpuSceneRid rid) noexcept;
        bool           alive(GpuSceneRid rid) const;
        // Call only after the last submitted frame has completed.
        void collectCompletedFrame();

    private:
        GpuSceneHandle insert(std::unique_ptr<GpuScene> scene);

        Device&                                                 m_Device;
        std::unordered_map<uint64_t, std::unique_ptr<GpuScene>> m_Scenes;
        std::vector<std::unique_ptr<GpuScene>>                  m_Retired;
    };
} // namespace vultra
