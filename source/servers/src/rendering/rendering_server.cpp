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

    std::expected<GpuScene*, std::string> GpuSceneHandle::getExpected() const
    {
        if (!m_Server)
        {
            return std::unexpected("GPU scene handle is empty");
        }
        return m_Server->sceneExpected(m_Rid);
    }

    GpuScene& GpuSceneHandle::get() const
    {
        auto scene = getExpected();
        if (!scene)
        {
            throw std::invalid_argument(scene.error());
        }
        return **scene;
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

    std::expected<GpuSceneHandle, std::string> RenderingServer::uploadSceneExpected(const SceneData&        scene,
                                                                                    const PreparedTextures& textures,
                                                                                    const MeshletData*      meshlets)
    {
        auto uploaded = GpuScene::create(m_Device, scene, textures, meshlets);
        if (!uploaded)
        {
            return std::unexpected(uploaded.error());
        }
        return insertExpected(std::move(*uploaded));
    }

    GpuSceneHandle
    RenderingServer::uploadScene(const SceneData& scene, const PreparedTextures& textures, const MeshletData* meshlets)
    {
        auto uploaded = uploadSceneExpected(scene, textures, meshlets);
        if (!uploaded)
        {
            throw std::invalid_argument(uploaded.error());
        }
        return std::move(*uploaded);
    }

    std::expected<GpuSceneHandle, std::string> RenderingServer::insertExpected(std::unique_ptr<GpuScene> scene)
    {
        uint64_t counter = nextRid.load(std::memory_order_relaxed);
        for (;;)
        {
            if (counter == 0 || counter > kRidCounterMask)
            {
                return std::unexpected("GPU resource ID space exhausted");
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
            return std::unexpected("GPU resource ID collision");
        }
        return GpuSceneHandle {*this, {entry->first}};
    }

    GpuSceneHandle RenderingServer::insert(std::unique_ptr<GpuScene> scene)
    {
        auto inserted = insertExpected(std::move(scene));
        if (!inserted)
        {
            throw std::runtime_error(inserted.error());
        }
        return std::move(*inserted);
    }

    std::expected<GpuScene*, std::string> RenderingServer::sceneExpected(GpuSceneRid rid) const
    {
        auto entry = m_Scenes.find(rid.value);
        if (!sceneRid(rid) || entry == m_Scenes.end())
        {
            return std::unexpected("Invalid GPU scene RID for this rendering server");
        }
        return entry->second.get();
    }

    GpuScene& RenderingServer::scene(GpuSceneRid rid) const
    {
        auto scene = sceneExpected(rid);
        if (!scene)
        {
            throw std::invalid_argument(scene.error());
        }
        return **scene;
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

    std::expected<void, std::string> RenderingServer::deferReleasedExpected(std::vector<SubmissionCompletion>& lastUses)
    {
        if (m_Retired.empty())
        {
            return {};
        }
        auto retired = std::make_unique<std::vector<std::unique_ptr<GpuScene>>>();
        // Future handle destruction keeps its allocation-free release guarantee after this batch moves away.
        retired->reserve(m_Scenes.size() + 1);
        m_Retired.swap(*retired);
        if (auto deferred = m_Device.deferReleaseExpected(retired, lastUses); !deferred)
        {
            m_Retired.swap(*retired);
            return deferred;
        }
        return {};
    }
} // namespace vultra
