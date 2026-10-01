#include <vultra/servers/rendering/rendering_server.hpp>
#include <vultra/servers/rendering/scene.hpp>

#include <atomic>
#include <stdexcept>
#include <utility>

namespace vultra
{
    namespace
    {
        // Identity allocation only; no global server or resource ownership.
        constexpr uint64_t    kRidCounterMask = (uint64_t(1) << 56) - 1;
        constexpr uint64_t    kGpuSceneTag    = uint64_t(1) << 56;
        std::atomic<uint64_t> nextRid {1};

        bool sceneRid(GpuSceneRid rid)
        {
            return (rid.value & ~kRidCounterMask) == kGpuSceneTag;
        }
    } // namespace

    GpuSceneHandle::GpuSceneHandle(RenderingServer& server, GpuSceneRid rid) :
        m_Server(&server),
        m_Rid(rid)
    {
    }

    GpuSceneHandle::~GpuSceneHandle()
    {
        reset();
    }

    GpuSceneHandle::GpuSceneHandle(GpuSceneHandle&& other) noexcept :
        m_Server(std::exchange(other.m_Server, nullptr)),
        m_Rid(std::exchange(other.m_Rid, {}))
    {
    }

    GpuSceneHandle& GpuSceneHandle::operator=(GpuSceneHandle&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            m_Server = std::exchange(other.m_Server, nullptr);
            m_Rid    = std::exchange(other.m_Rid, {});
        }
        return *this;
    }

    void GpuSceneHandle::reset() noexcept
    {
        if (m_Server)
        {
            m_Server->release(m_Rid);
            m_Server = nullptr;
            m_Rid    = {};
        }
    }

    GpuScene& GpuSceneHandle::get() const
    {
        if (!m_Server)
        {
            throw std::invalid_argument("GPU scene handle is empty");
        }
        return m_Server->scene(m_Rid);
    }

    GpuScene& GpuSceneHandle::operator*() const
    {
        return get();
    }

    GpuScene* GpuSceneHandle::operator->() const
    {
        return &get();
    }

    GpuSceneRid GpuSceneHandle::rid() const
    {
        return m_Rid;
    }

    RenderingServer::RenderingServer(Device& device) :
        m_Device(device)
    {
    }

    RenderingServer::~RenderingServer() = default;

    GpuSceneHandle RenderingServer::uploadScene(const SceneData& scene, bool meshShading, uint32_t workers)
    {
        return insert(std::make_unique<GpuScene>(m_Device, scene, meshShading, workers));
    }

    GpuSceneHandle RenderingServer::uploadScene(const ImportedAsset& asset, bool meshShading, uint32_t workers)
    {
        return insert(std::make_unique<GpuScene>(m_Device, asset, meshShading, workers));
    }

    GpuSceneHandle RenderingServer::insert(std::unique_ptr<GpuScene> scene)
    {
        uint64_t counter = nextRid.load(std::memory_order_relaxed);
        for (;;)
        {
            if (counter == 0 || counter > kRidCounterMask)
            {
                throw std::overflow_error("GPU resource ID space exhausted");
            }
            if (nextRid.compare_exchange_weak(counter, counter + 1, std::memory_order_relaxed))
            {
                break;
            }
        }
        const uint64_t value = kGpuSceneTag | counter;
        // A handle destructor must retire its scene without allocating or throwing.
        m_Retired.reserve(m_Scenes.size() + m_Retired.size() + 1);
        auto [entry, inserted] = m_Scenes.emplace(value, std::move(scene));
        if (!inserted)
        {
            throw std::logic_error("GPU resource ID collision");
        }
        return {*this, {entry->first}};
    }

    GpuScene& RenderingServer::scene(GpuSceneRid rid) const
    {
        auto entry = m_Scenes.find(rid.value);
        if (!sceneRid(rid) || entry == m_Scenes.end())
        {
            throw std::invalid_argument("Invalid GPU scene RID for this rendering server");
        }
        return *entry->second;
    }

    bool RenderingServer::release(GpuSceneRid rid) noexcept
    {
        auto entry = m_Scenes.find(rid.value);
        if (!sceneRid(rid) || entry == m_Scenes.end())
        {
            return false;
        }
        m_Retired.push_back(std::move(entry->second));
        m_Scenes.erase(entry);
        return true;
    }

    bool RenderingServer::alive(GpuSceneRid rid) const
    {
        return sceneRid(rid) && m_Scenes.contains(rid.value);
    }

    void RenderingServer::collectCompletedFrame()
    {
        m_Retired.clear();
    }
} // namespace vultra
