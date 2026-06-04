#include "vultra/function/physics/physics_system.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/base/uuid.hpp"
#include "vultra/core/services/timing_service.hpp"
#include "vultra/function/services/asset_service.hpp"
#include "vultra/function/services/job_service.hpp"
#include "vultra/function/services/world_service.hpp"
#include "vultra/function/world/components/box_shape_component.hpp"
#include "vultra/function/world/components/capsule_shape_component.hpp"
#include "vultra/function/world/components/character_controller_component.hpp"
#include "vultra/function/world/components/cylinder_shape_component.hpp"
#include "vultra/function/world/components/entity_status_component.hpp"
#include "vultra/function/world/components/mesh_component.hpp"
#include "vultra/function/world/components/mesh_shape_component.hpp"
#include "vultra/function/world/components/rigid_body_component.hpp"
#include "vultra/function/world/components/sphere_shape_component.hpp"
#include "vultra/function/world/components/transform_component.hpp"
#include "vultra/function/world/world.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemWithBarrier.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyLockInterface.h>
#include <Jolt/Physics/Body/MotionType.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <vasset/vmesh.hpp>

#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <vtask/scheduler.hpp>
#include <vtask/task_set.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vultra
{
    namespace
    {
        // Logical collision layers are stored on RigidBodyComponent::objectLayer as a
        // small index [0, kMaxLayers). The Jolt ObjectLayer packs that index together
        // with a "moving" bit (derived from motion type) so the broad phase can keep the
        // classic moving/non-moving split while user code reasons in friendly indices.
        constexpr uint32_t kMaxLayers = 32;

        constexpr JPH::BroadPhaseLayer kBroadPhaseNonMoving {0};
        constexpr JPH::BroadPhaseLayer kBroadPhaseMoving {1};
        constexpr JPH::uint            kNumBroadPhaseLayers = 2;

        inline JPH::ObjectLayer encodeObjectLayer(uint32_t layerIndex, bool moving)
        {
            return static_cast<JPH::ObjectLayer>(((layerIndex & 0x7FFFu) << 1) | (moving ? 1u : 0u));
        }
        inline uint32_t decodeLayerIndex(JPH::ObjectLayer layer)
        {
            return (static_cast<uint32_t>(layer) >> 1) & 0x7FFFu;
        }
        inline bool decodeMoving(JPH::ObjectLayer layer) { return (static_cast<uint32_t>(layer) & 1u) != 0u; }

        // Per-pair collision matrix shared by the narrow/broad phase filters.
        struct LayerConfig
        {
            std::array<bool, kMaxLayers * kMaxLayers> collide {};

            LayerConfig() { collide.fill(true); }

            bool shouldCollide(uint32_t a, uint32_t b) const
            {
                if (a >= kMaxLayers || b >= kMaxLayers)
                    return true;
                return collide[a * kMaxLayers + b];
            }
            void set(uint32_t a, uint32_t b, bool enabled)
            {
                if (a >= kMaxLayers || b >= kMaxLayers)
                    return;
                collide[a * kMaxLayers + b] = enabled;
                collide[b * kMaxLayers + a] = enabled;
            }
        };

        std::atomic_uint32_t g_JoltUsers {0};

        JPH::Vec3 toJolt(const glm::vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
        JPH::RVec3 toJoltR(const glm::vec3& v) { return JPH::RVec3(v.x, v.y, v.z); }
        JPH::Quat toJolt(const glm::quat& q) { return JPH::Quat(q.x, q.y, q.z, q.w); }

        glm::vec3 fromJolt(const JPH::Vec3& v) { return {v.GetX(), v.GetY(), v.GetZ()}; }
        glm::vec3 fromJoltR(const JPH::RVec3& v) { return {static_cast<float>(v.GetX()), static_cast<float>(v.GetY()), static_cast<float>(v.GetZ())}; }
        glm::quat fromJolt(const JPH::Quat& q) { return {q.GetW(), q.GetX(), q.GetY(), q.GetZ()}; }

        JPH::uint64 toUserData(entt::entity entity) { return static_cast<JPH::uint64>(entt::to_integral(entity)); }
        entt::entity fromUserData(JPH::uint64 userData)
        {
            return static_cast<entt::entity>(static_cast<entt::id_type>(userData));
        }

        bool entityActive(World& world, entt::entity entity)
        {
            if (!world.registry().valid(entity))
                return false;
            const auto* status = world.registry().try_get<EntityStatusComponent>(entity);
            return !status || status->active;
        }

        // ObjectLayerFilter that keeps only bodies whose logical layer index is set in a
        // 32-bit mask. Used to scope queries (e.g. only hit world + enemies, not the player).
        class MaskLayerFilter final : public JPH::ObjectLayerFilter
        {
        public:
            explicit MaskLayerFilter(uint32_t mask) : m_Mask(mask) {}
            bool ShouldCollide(JPH::ObjectLayer layer) const override
            {
                const uint32_t idx = decodeLayerIndex(layer);
                if (idx >= 32u)
                    return true;
                return ((m_Mask >> idx) & 1u) != 0u;
            }

        private:
            uint32_t m_Mask;
        };

        // BodyFilter resolving entity status / ignore-entity from the locked body's user
        // data. We override ShouldCollideLocked so the body is already locked by the query
        // (avoids re-locking via the BodyInterface, which would deadlock).
        class EntityBodyFilter final : public JPH::BodyFilter
        {
        public:
            EntityBodyFilter(World& world, bool activeOnly, entt::entity ignore) :
                m_World(world), m_ActiveOnly(activeOnly), m_Ignore(ignore)
            {
            }

            bool ShouldCollideLocked(const JPH::Body& body) const override
            {
                const entt::entity entity = fromUserData(body.GetUserData());
                if (entity == m_Ignore)
                    return false;
                if (m_ActiveOnly && !entityActive(m_World, entity))
                    return false;
                return true;
            }

        private:
            World&       m_World;
            bool         m_ActiveOnly;
            entt::entity m_Ignore;
        };

        // Captures begin/end contact + trigger events from Jolt's job threads into a
        // thread-safe queue drained on the main thread after the step. OnContactRemoved cannot
        // access the bodies, so entities are resolved via a body-index -> entity map populated
        // when bodies are created (read-only during Update, so safe without locking that map).
        class JoltContactListener final : public JPH::ContactListener
        {
        public:
            std::mutex*                                   mutex {nullptr};
            std::vector<PhysicsContactEvent>*             events {nullptr};
            const std::unordered_map<uint32_t, entt::entity>* bodyToEntity {nullptr};

            void OnContactAdded(const JPH::Body&            body1,
                                const JPH::Body&            body2,
                                const JPH::ContactManifold& /*manifold*/,
                                JPH::ContactSettings& /*settings*/) override
            {
                push(PhysicsContactEvent::Type::eEnter,
                     fromUserData(body1.GetUserData()),
                     fromUserData(body2.GetUserData()));
            }

            void OnContactRemoved(const JPH::SubShapeIDPair& pair) override
            {
                push(PhysicsContactEvent::Type::eExit, resolve(pair.GetBody1ID()), resolve(pair.GetBody2ID()));
            }

        private:
            entt::entity resolve(const JPH::BodyID& id) const
            {
                if (!bodyToEntity)
                    return entt::null;
                const auto it = bodyToEntity->find(id.GetIndexAndSequenceNumber());
                return it != bodyToEntity->end() ? it->second : entt::null;
            }

            void push(PhysicsContactEvent::Type type, entt::entity a, entt::entity b)
            {
                if (!mutex || !events)
                    return;
                std::scoped_lock lock(*mutex);
                if (events->size() >= 8192u) // bound the queue if nobody consumes
                    return;
                events->push_back(PhysicsContactEvent {.type = type, .a = a, .b = b});
            }
        };

        float maxComponent(const glm::vec3& v) { return std::max({v.x, v.y, v.z}); }

        bool aabbIntersectsAabb(const glm::vec3& aCenter,
                                const glm::vec3& aHalfExtents,
                                const glm::vec3& bCenter,
                                const glm::vec3& bHalfExtents)
        {
            const glm::vec3 d = glm::abs(aCenter - bCenter);
            return d.x <= aHalfExtents.x + bHalfExtents.x && d.y <= aHalfExtents.y + bHalfExtents.y &&
                   d.z <= aHalfExtents.z + bHalfExtents.z;
        }

        glm::vec3 entityHalfExtents(World& world, entt::entity entity)
        {
            auto& reg = world.registry();
            const auto* transform = reg.try_get<TransformComponent>(entity);
            if (!transform)
                return {};

            const glm::vec3 scale = glm::abs(transform->scale);
            if (const auto* box = reg.try_get<BoxShapeComponent>(entity))
                return box->halfExtents * scale;
            if (const auto* sphere = reg.try_get<SphereShapeComponent>(entity))
            {
                const float r = sphere->radius * maxComponent(scale);
                return {r, r, r};
            }
            if (const auto* capsule = reg.try_get<CapsuleShapeComponent>(entity))
            {
                const float r = (capsule->halfHeightOfCylinder + capsule->radius) * maxComponent(scale);
                return {r, r, r};
            }
            return {};
        }

        // Builds a Jolt collision shape from an imported mesh's CPU geometry, baking the
        // entity's transform scale into the vertices. Triangle mesh for static level
        // geometry, or a convex hull when requested (usable on dynamic bodies).
        JPH::ShapeRefC buildMeshCollisionShape(IAssetService*    assets,
                                               const CoreUUID&   meshUuid,
                                               bool              convex,
                                               const glm::vec3&  scale)
        {
            if (!assets || !meshUuid.valid())
                return {};

            auto handle = assets->loadMeshSync(meshUuid);
            const vasset::VMesh* cpu = handle.cpu();
            if (!cpu || cpu->positions.empty() || cpu->indices.size() < 3)
                return {};

            if (convex)
            {
                JPH::Array<JPH::Vec3> points;
                points.reserve(cpu->positions.size());
                for (const auto& p : cpu->positions)
                    points.push_back(JPH::Vec3(p.x * scale.x, p.y * scale.y, p.z * scale.z));
                auto result = JPH::ConvexHullShapeSettings(points).Create();
                if (result.HasError())
                    return {};
                return result.Get();
            }

            JPH::VertexList verts;
            verts.reserve(cpu->positions.size());
            for (const auto& p : cpu->positions)
                verts.push_back(JPH::Float3(p.x * scale.x, p.y * scale.y, p.z * scale.z));

            JPH::IndexedTriangleList tris;
            tris.reserve(cpu->indices.size() / 3);
            for (size_t i = 0; i + 2 < cpu->indices.size(); i += 3)
                tris.push_back(JPH::IndexedTriangle(
                    cpu->indices[i], cpu->indices[i + 1], cpu->indices[i + 2], 0));

            auto result = JPH::MeshShapeSettings(verts, tris).Create();
            if (result.HasError())
                return {};
            return result.Get();
        }

        JPH::EMotionType toMotionType(uint32_t value)
        {
            switch (value)
            {
                case 0:
                    return JPH::EMotionType::Static;
                case 1:
                    return JPH::EMotionType::Kinematic;
                default:
                    return JPH::EMotionType::Dynamic;
            }
        }

        JPH::EMotionQuality toMotionQuality(uint32_t value)
        {
            return value == 1u ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
        }

        class BroadPhaseLayerInterface final : public JPH::BroadPhaseLayerInterface
        {
        public:
            JPH::uint GetNumBroadPhaseLayers() const override { return kNumBroadPhaseLayers; }

            JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
            {
                return decodeMoving(layer) ? kBroadPhaseMoving : kBroadPhaseNonMoving;
            }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
            const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
            {
                return layer == kBroadPhaseNonMoving ? "NonMoving" : "Moving";
            }
#endif
        };

        class ObjectVsBroadPhaseLayerFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
        {
        public:
            bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhaseLayer) const override
            {
                // Non-moving objects only need to test against the moving broad phase.
                if (!decodeMoving(layer))
                    return broadPhaseLayer == kBroadPhaseMoving;
                return true;
            }
        };

        class ObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter
        {
        public:
            const LayerConfig* config {nullptr};

            bool ShouldCollide(JPH::ObjectLayer layer1, JPH::ObjectLayer layer2) const override
            {
                // Two static (non-moving) bodies never collide.
                if (!decodeMoving(layer1) && !decodeMoving(layer2))
                    return false;
                if (!config)
                    return true;
                return config->shouldCollide(decodeLayerIndex(layer1), decodeLayerIndex(layer2));
            }
        };

        class VTaskJoltJobSystem final : public JPH::JobSystemWithBarrier
        {
        public:
            explicit VTaskJoltJobSystem(IJobService& jobs) : m_Jobs(jobs) { Init(std::max(1u, jobs.concurrency())); }
            ~VTaskJoltJobSystem() override { waitAll(); }

            int GetMaxConcurrency() const override { return static_cast<int>(std::max(1u, m_Jobs.concurrency())); }

            JobHandle CreateJob(const char* name,
                                JPH::ColorArg color,
                                const JobFunction& function,
                                JPH::uint32 dependencies = 0) override
            {
                return JobHandle(new Job(name, color, this, function, dependencies));
            }

        protected:
            void FreeJob(Job* job) override { delete job; }

            void QueueJob(Job* job) override
            {
                auto record  = std::make_unique<QueuedJob>();
                record->done = std::make_shared<std::atomic_bool>(false);
                auto done    = record->done;

                job->AddRef();
                record->task = std::make_unique<vtask::TaskSet>(1, 1, [job, done](vtask::Range) {
                    job->Execute();
                    job->Release();
                    done->store(true, std::memory_order_release);
                });

                vtask::TaskSet* task = record->task.get();
                {
                    std::scoped_lock lock(m_Mutex);
                    m_Queued.push_back(std::move(record));
                }
                m_Jobs.scheduler().run(*task);
            }

            void QueueJobs(Job** jobs, JPH::uint count) override
            {
                for (JPH::uint i = 0; i < count; ++i)
                    QueueJob(jobs[i]);
            }

            void WaitForJobs(Barrier* barrier) override
            {
                JobSystemWithBarrier::WaitForJobs(barrier);
                collectFinished();
            }

        private:
            struct QueuedJob
            {
                std::unique_ptr<vtask::TaskSet> task;
                std::shared_ptr<std::atomic_bool> done;
            };

            void waitAll()
            {
                while (true)
                {
                    std::vector<vtask::TaskSet*> jobs;
                    {
                        std::scoped_lock lock(m_Mutex);
                        collectFinishedLocked();
                        if (m_Queued.empty())
                            return;

                        jobs.reserve(m_Queued.size());
                        for (auto& job : m_Queued)
                        {
                            if (job->task)
                                jobs.push_back(job->task.get());
                        }
                    }

                    for (auto* job : jobs)
                        m_Jobs.scheduler().wait(*job);
                }
            }

            void collectFinished()
            {
                std::vector<vtask::TaskSet*> completed;
                {
                    std::scoped_lock lock(m_Mutex);
                    completed.reserve(m_Queued.size());
                    for (auto& job : m_Queued)
                    {
                        if (job->task && job->done->load(std::memory_order_acquire))
                            completed.push_back(job->task.get());
                    }
                }

                for (auto* task : completed)
                    m_Jobs.scheduler().wait(*task);

                std::scoped_lock lock(m_Mutex);
                collectFinishedLocked();
            }

            void collectFinishedLocked()
            {
                for (auto it = m_Queued.begin(); it != m_Queued.end();)
                {
                    if (!(*it)->done->load(std::memory_order_acquire))
                    {
                        ++it;
                        continue;
                    }
                    it = m_Queued.erase(it);
                }
            }

            IJobService& m_Jobs;
            std::mutex   m_Mutex;
            std::vector<std::unique_ptr<QueuedJob>> m_Queued;
        };
    } // namespace

    struct PhysicsSystem::BodySignature
    {
        uint32_t shapeType {0};
        glm::vec3 shapeA {0.0f};
        float shapeB {0.0f};

        // Mesh-shape identity (shapeType 5): hash of the source mesh UUID + bake scale + convexity.
        uint64_t  meshKey {0};
        glm::vec3 meshScale {1.0f};
        uint32_t  meshConvex {0};

        uint32_t motionType {2};
        uint32_t objectLayer {1};
        bool     isSensor {false};
        uint32_t motionQuality {0};
        bool     allowSleeping {true};
        float friction {0.2f};
        float restitution {0.0f};
        float linearDamping {0.05f};
        float angularDamping {0.05f};
        float gravityFactor {1.0f};
        bool  overrideMass {false};
        float mass {1.0f};
        float maxLinearVelocity {500.0f};
        float maxAngularVelocity {0.25f * 3.1415926535f * 60.0f};

        bool operator==(const BodySignature& rhs) const
        {
            return std::memcmp(this, &rhs, sizeof(BodySignature)) == 0;
        }
    };

    struct PhysicsSystem::BodyRecord
    {
        JPH::BodyID   id;
        BodySignature signature;
    };

    struct PhysicsSystem::CharacterRecord
    {
        JPH::Ref<JPH::CharacterVirtual> character;
        // Shape signature so we recreate when the capsule config changes.
        float    radius {0.0f};
        float    height {0.0f};
        float    maxSlopeAngleDegrees {0.0f};
        uint32_t objectLayer {0};
    };

    struct PhysicsSystem::Impl
    {
        LayerConfig layerConfig;
        // Contact-event plumbing (declared early so it outlives `physics`, which holds a raw
        // pointer to the listener and is destroyed first in reverse-declaration order).
        std::mutex                                   contactMutex;
        std::vector<PhysicsContactEvent>             contactEvents;
        std::unordered_map<uint32_t, entt::entity>   bodyToEntity;
        JoltContactListener                          contactListener;
        BroadPhaseLayerInterface broadPhaseLayerInterface;
        ObjectVsBroadPhaseLayerFilter objectVsBroadPhaseLayerFilter;
        ObjectLayerPairFilter objectLayerPairFilter;
        std::unique_ptr<JPH::TempAllocator> tempAllocator;
        std::unique_ptr<VTaskJoltJobSystem> jobSystem;
        std::unique_ptr<JPH::PhysicsSystem> physics;
        std::unordered_map<entt::entity, BodyRecord> bodies;
        std::unordered_map<entt::entity, CharacterRecord> characters;
    };

    PhysicsSystem::PhysicsSystem() = default;
    PhysicsSystem::~PhysicsSystem() = default;

    void PhysicsSystem::ensureJoltGlobals()
    {
        if (g_JoltUsers.fetch_add(1, std::memory_order_acq_rel) == 0)
        {
            JPH::RegisterDefaultAllocator();
            JPH::Factory::sInstance = new JPH::Factory();
            JPH::RegisterTypes();
        }
    }

    void PhysicsSystem::releaseJoltGlobals()
    {
        if (g_JoltUsers.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            JPH::UnregisterTypes();
            delete JPH::Factory::sInstance;
            JPH::Factory::sInstance = nullptr;
        }
    }

    bool PhysicsSystem::onInit()
    {
        m_WorldService = ctx().services.tryGet<IWorldService>();
        m_JobService   = ctx().services.tryGet<IJobService>();
        m_TimingService = ctx().services.tryGet<ITimingService>();
        m_AssetService  = ctx().services.tryGet<IAssetService>();
        if (!m_WorldService || !m_JobService)
        {
            VULTRA_CORE_WARN("[PhysicsSystem] Missing world or job service; physics disabled");
            return true;
        }

        ensureJoltGlobals();
        m_JoltGlobalsAcquired = true;

        m_Impl = std::make_unique<Impl>();
        m_Impl->tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);
        m_Impl->jobSystem     = std::make_unique<VTaskJoltJobSystem>(*m_JobService);
        m_Impl->physics       = std::make_unique<JPH::PhysicsSystem>();

        constexpr JPH::uint kMaxBodies = 65536;
        constexpr JPH::uint kNumBodyMutexes = 0;
        constexpr JPH::uint kMaxBodyPairs = 65536;
        constexpr JPH::uint kMaxContactConstraints = 10240;
        m_Impl->objectLayerPairFilter.config = &m_Impl->layerConfig;
        m_Impl->physics->Init(kMaxBodies,
                              kNumBodyMutexes,
                              kMaxBodyPairs,
                              kMaxContactConstraints,
                              m_Impl->broadPhaseLayerInterface,
                              m_Impl->objectVsBroadPhaseLayerFilter,
                              m_Impl->objectLayerPairFilter);
        m_Impl->physics->SetGravity(toJolt(m_Gravity));

        m_Impl->contactListener.mutex        = &m_Impl->contactMutex;
        m_Impl->contactListener.events       = &m_Impl->contactEvents;
        m_Impl->contactListener.bodyToEntity = &m_Impl->bodyToEntity;
        m_Impl->physics->SetContactListener(&m_Impl->contactListener);

        ctx().services.provide<IPhysicsService>(this);
        VULTRA_CORE_INFO("[PhysicsSystem] Initialized with Jolt");
        return true;
    }

    void PhysicsSystem::onShutdown()
    {
        clearBodies();
        m_Impl.reset();
        if (m_JoltGlobalsAcquired)
        {
            releaseJoltGlobals();
            m_JoltGlobalsAcquired = false;
        }
        m_WorldService = nullptr;
        m_JobService   = nullptr;
        m_TimingService = nullptr;
        m_AssetService = nullptr;
        m_Accumulator  = 0.0f;
    }

    void PhysicsSystem::setEnabled(bool enabled)
    {
        if (m_Enabled == enabled)
            return;

        m_Enabled = enabled;
        if (!m_Enabled)
            clearBodies();
    }

    void PhysicsSystem::setFixedTimeStep(float seconds)
    {
        m_FixedTimeStep = std::clamp(seconds, 1.0f / 240.0f, 1.0f / 15.0f);
        if (m_TimingService)
            m_TimingService->setFixedDeltaTime(m_FixedTimeStep);
    }

    void PhysicsSystem::setPlaybackState(const bool playing, const bool paused)
    {
        if (!playing)
        {
            m_Playing = false;
            m_Paused = false;
            m_Accumulator = 0.0f;
            m_PendingSingleSteps = 0;
            clearBodies();
            return;
        }

        if (!m_Playing)
            m_Accumulator = 0.0f;
        m_Playing = true;
        m_Paused = paused;
    }

    void PhysicsSystem::requestSingleStep()
    {
        m_Playing = true;
        m_Paused = true;
        ++m_PendingSingleSteps;
    }

    uint32_t PhysicsSystem::bodyCount() const
    {
        return m_Impl ? static_cast<uint32_t>(m_Impl->bodies.size()) : 0u;
    }

    bool PhysicsSystem::hasBody(entt::entity entity) const
    {
        return m_Impl && m_Impl->bodies.contains(entity);
    }

    bool PhysicsSystem::activate(entt::entity entity)
    {
        if (!m_Impl || !m_Impl->physics || !ensureBody(entity))
            return false;

        const auto it = m_Impl->bodies.find(entity);
        if (it == m_Impl->bodies.end())
            return false;

        m_Impl->physics->GetBodyInterface().ActivateBody(it->second.id);
        return true;
    }

    glm::vec3 PhysicsSystem::linearVelocity(entt::entity entity) const
    {
        if (m_Impl && m_Impl->physics)
        {
            const auto it = m_Impl->bodies.find(entity);
            if (it != m_Impl->bodies.end())
                return fromJolt(m_Impl->physics->GetBodyInterface().GetLinearVelocity(it->second.id));
        }

        if (m_WorldService)
        {
            auto& reg = m_WorldService->world().registry();
            if (auto* rb = reg.try_get<RigidBodyComponent>(entity))
                return rb->linearVelocity;
        }
        return {};
    }

    bool PhysicsSystem::setLinearVelocity(entt::entity entity, const glm::vec3& velocity)
    {
        if (!m_WorldService)
            return false;

        auto& reg = m_WorldService->world().registry();
        auto* rb  = reg.try_get<RigidBodyComponent>(entity);
        if (!rb)
            return false;

        rb->linearVelocity = velocity;
        if (m_Impl && m_Impl->physics && ensureBody(entity))
        {
            const auto it = m_Impl->bodies.find(entity);
            if (it != m_Impl->bodies.end())
            {
                auto& bodyInterface = m_Impl->physics->GetBodyInterface();
                bodyInterface.SetLinearVelocity(it->second.id, toJolt(velocity));
                bodyInterface.ActivateBody(it->second.id);
            }
        }
        return true;
    }

    glm::vec3 PhysicsSystem::angularVelocity(entt::entity entity) const
    {
        if (m_Impl && m_Impl->physics)
        {
            const auto it = m_Impl->bodies.find(entity);
            if (it != m_Impl->bodies.end())
                return fromJolt(m_Impl->physics->GetBodyInterface().GetAngularVelocity(it->second.id));
        }

        if (m_WorldService)
        {
            auto& reg = m_WorldService->world().registry();
            if (auto* rb = reg.try_get<RigidBodyComponent>(entity))
                return rb->angularVelocity;
        }
        return {};
    }

    bool PhysicsSystem::setAngularVelocity(entt::entity entity, const glm::vec3& velocity)
    {
        if (!m_WorldService)
            return false;

        auto& reg = m_WorldService->world().registry();
        auto* rb  = reg.try_get<RigidBodyComponent>(entity);
        if (!rb)
            return false;

        rb->angularVelocity = velocity;
        if (m_Impl && m_Impl->physics && ensureBody(entity))
        {
            const auto it = m_Impl->bodies.find(entity);
            if (it != m_Impl->bodies.end())
            {
                auto& bodyInterface = m_Impl->physics->GetBodyInterface();
                bodyInterface.SetAngularVelocity(it->second.id, toJolt(velocity));
                bodyInterface.ActivateBody(it->second.id);
            }
        }
        return true;
    }

    bool PhysicsSystem::addForce(entt::entity entity, const glm::vec3& force)
    {
        if (!m_Impl || !m_Impl->physics || !ensureBody(entity))
            return false;

        const auto it = m_Impl->bodies.find(entity);
        if (it == m_Impl->bodies.end())
            return false;

        auto& bodyInterface = m_Impl->physics->GetBodyInterface();
        bodyInterface.AddForce(it->second.id, toJolt(force));
        bodyInterface.ActivateBody(it->second.id);
        return true;
    }

    bool PhysicsSystem::addImpulse(entt::entity entity, const glm::vec3& impulse)
    {
        if (!m_Impl || !m_Impl->physics || !ensureBody(entity))
            return false;

        const auto it = m_Impl->bodies.find(entity);
        if (it == m_Impl->bodies.end())
            return false;

        auto& bodyInterface = m_Impl->physics->GetBodyInterface();
        bodyInterface.AddImpulse(it->second.id, toJolt(impulse));
        bodyInterface.ActivateBody(it->second.id);
        return true;
    }

    bool PhysicsSystem::addTorque(entt::entity entity, const glm::vec3& torque)
    {
        if (!m_Impl || !m_Impl->physics || !ensureBody(entity))
            return false;

        const auto it = m_Impl->bodies.find(entity);
        if (it == m_Impl->bodies.end())
            return false;

        auto& bodyInterface = m_Impl->physics->GetBodyInterface();
        bodyInterface.AddTorque(it->second.id, toJolt(torque));
        bodyInterface.ActivateBody(it->second.id);
        return true;
    }

    bool PhysicsSystem::addAngularImpulse(entt::entity entity, const glm::vec3& impulse)
    {
        if (!m_Impl || !m_Impl->physics || !ensureBody(entity))
            return false;

        const auto it = m_Impl->bodies.find(entity);
        if (it == m_Impl->bodies.end())
            return false;

        auto& bodyInterface = m_Impl->physics->GetBodyInterface();
        bodyInterface.AddAngularImpulse(it->second.id, toJolt(impulse));
        bodyInterface.ActivateBody(it->second.id);
        return true;
    }

    bool PhysicsSystem::setPosition(entt::entity entity, const glm::vec3& position, bool activate)
    {
        if (!m_WorldService)
            return false;

        auto& reg = m_WorldService->world().registry();
        auto* transform = reg.try_get<TransformComponent>(entity);
        if (!transform)
            return false;

        transform->position = position;
        transform->dirty = true;

        if (m_Impl && m_Impl->physics && ensureBody(entity))
        {
            const auto it = m_Impl->bodies.find(entity);
            if (it != m_Impl->bodies.end())
            {
                const auto activation = activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate;
                m_Impl->physics->GetBodyInterface().SetPosition(it->second.id, toJoltR(position), activation);
            }
        }
        return true;
    }

    bool PhysicsSystem::setRotation(entt::entity entity, const glm::vec3& eulerDegrees, bool activate)
    {
        if (!m_WorldService)
            return false;

        auto& reg = m_WorldService->world().registry();
        auto* transform = reg.try_get<TransformComponent>(entity);
        if (!transform)
            return false;

        const glm::quat rotation = glm::quat(glm::radians(eulerDegrees));
        transform->rotation = rotation;
        transform->dirty = true;

        if (m_Impl && m_Impl->physics && ensureBody(entity))
        {
            const auto it = m_Impl->bodies.find(entity);
            if (it != m_Impl->bodies.end())
            {
                const auto activation = activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate;
                m_Impl->physics->GetBodyInterface().SetRotation(it->second.id, toJolt(rotation), activation);
            }
        }
        return true;
    }

    void PhysicsSystem::setGravity(const glm::vec3& gravity)
    {
        m_Gravity = gravity;
        if (m_Impl && m_Impl->physics)
            m_Impl->physics->SetGravity(toJolt(gravity));
    }

    glm::vec3 PhysicsSystem::gravity() const { return m_Gravity; }

    void PhysicsSystem::setLayerCollision(uint32_t layerA, uint32_t layerB, bool enabled)
    {
        if (m_Impl)
            m_Impl->layerConfig.set(layerA, layerB, enabled);
    }

    bool PhysicsSystem::layerCollision(uint32_t layerA, uint32_t layerB) const
    {
        return m_Impl ? m_Impl->layerConfig.shouldCollide(layerA, layerB) : true;
    }

    std::optional<PhysicsRaycastHit> PhysicsSystem::raycast(const glm::vec3&          origin,
                                                            const glm::vec3&          direction,
                                                            float                     maxDistance,
                                                            const PhysicsQueryFilter& filter) const
    {
        if (!m_Impl || !m_Impl->physics || !m_WorldService || maxDistance <= 0.0f)
            return std::nullopt;

        const float dirLenSq = glm::dot(direction, direction);
        if (dirLenSq <= 0.000001f)
            return std::nullopt;

        const glm::vec3 dir = direction / std::sqrt(dirLenSq);
        auto&           world = m_WorldService->world();

        const JPH::RRayCast ray {toJoltR(origin), toJolt(dir * maxDistance)};
        JPH::RayCastResult  result;
        const MaskLayerFilter  objectFilter {filter.layerMask};
        const EntityBodyFilter bodyFilter {world, filter.activeOnly, filter.ignore};

        const bool hit = m_Impl->physics->GetNarrowPhaseQuery().CastRay(
            ray, result, {}, objectFilter, bodyFilter);
        if (!hit)
            return std::nullopt;

        const float     distance = maxDistance * result.mFraction;
        const glm::vec3 point    = origin + dir * distance;

        PhysicsRaycastHit out {};
        out.point    = point;
        out.fraction = result.mFraction;
        out.distance = distance;
        out.normal   = {0.0f, 1.0f, 0.0f};

        const JPH::BodyLockRead lock(m_Impl->physics->GetBodyLockInterface(), result.mBodyID);
        if (lock.Succeeded())
        {
            const JPH::Body& body = lock.GetBody();
            out.entity = fromUserData(body.GetUserData());
            out.normal = fromJolt(body.GetWorldSpaceSurfaceNormal(result.mSubShapeID2, toJoltR(point)));
        }
        return out;
    }

    std::vector<PhysicsRaycastHit> PhysicsSystem::raycastAll(const glm::vec3&          origin,
                                                             const glm::vec3&          direction,
                                                             float                     maxDistance,
                                                             const PhysicsQueryFilter& filter) const
    {
        std::vector<PhysicsRaycastHit> hits;
        if (!m_Impl || !m_Impl->physics || !m_WorldService || maxDistance <= 0.0f)
            return hits;

        const float dirLenSq = glm::dot(direction, direction);
        if (dirLenSq <= 0.000001f)
            return hits;

        const glm::vec3 dir = direction / std::sqrt(dirLenSq);
        auto&           world = m_WorldService->world();

        const JPH::RRayCast ray {toJoltR(origin), toJolt(dir * maxDistance)};
        JPH::RayCastSettings settings;
        settings.mTreatConvexAsSolid = true;
        JPH::AllHitCollisionCollector<JPH::CastRayCollector> collector;
        const MaskLayerFilter  objectFilter {filter.layerMask};
        const EntityBodyFilter bodyFilter {world, filter.activeOnly, filter.ignore};

        m_Impl->physics->GetNarrowPhaseQuery().CastRay(
            ray, settings, collector, {}, objectFilter, bodyFilter);

        collector.Sort();
        hits.reserve(collector.mHits.size());
        for (const auto& result : collector.mHits)
        {
            const float     distance = maxDistance * result.mFraction;
            const glm::vec3 point    = origin + dir * distance;
            PhysicsRaycastHit out {};
            out.point    = point;
            out.fraction = result.mFraction;
            out.distance = distance;
            out.normal   = {0.0f, 1.0f, 0.0f};

            const JPH::BodyLockRead lock(m_Impl->physics->GetBodyLockInterface(), result.mBodyID);
            if (lock.Succeeded())
            {
                const JPH::Body& body = lock.GetBody();
                out.entity = fromUserData(body.GetUserData());
                out.normal = fromJolt(body.GetWorldSpaceSurfaceNormal(result.mSubShapeID2, toJoltR(point)));
            }
            hits.push_back(out);
        }
        return hits;
    }

    std::optional<PhysicsShapeCastHit> PhysicsSystem::sphereCast(const glm::vec3&          origin,
                                                                 const glm::vec3&          direction,
                                                                 float                     radius,
                                                                 float                     maxDistance,
                                                                 const PhysicsQueryFilter& filter) const
    {
        if (!m_Impl || !m_Impl->physics || !m_WorldService || maxDistance <= 0.0f || radius <= 0.0f)
            return std::nullopt;

        const float dirLenSq = glm::dot(direction, direction);
        if (dirLenSq <= 0.000001f)
            return std::nullopt;

        const glm::vec3 dir = direction / std::sqrt(dirLenSq);
        auto&           world = m_WorldService->world();

        auto shapeResult = JPH::SphereShapeSettings(std::max(radius, 0.001f)).Create();
        if (shapeResult.HasError())
            return std::nullopt;
        const JPH::ShapeRefC shape = shapeResult.Get();

        const JPH::RShapeCast shapeCast(
            shape, JPH::Vec3::sOne(), JPH::RMat44::sTranslation(toJoltR(origin)), toJolt(dir * maxDistance));
        JPH::ShapeCastSettings settings;
        JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
        const MaskLayerFilter  objectFilter {filter.layerMask};
        const EntityBodyFilter bodyFilter {world, filter.activeOnly, filter.ignore};

        m_Impl->physics->GetNarrowPhaseQuery().CastShape(
            shapeCast, settings, toJoltR(origin), collector, {}, objectFilter, bodyFilter);
        if (!collector.HadHit())
            return std::nullopt;

        const auto& result = collector.mHit;
        PhysicsShapeCastHit out {};
        out.fraction         = result.mFraction;
        out.distance         = maxDistance * result.mFraction;
        out.point            = fromJolt(result.mContactPointOn2);
        out.normal           = glm::normalize(fromJolt(-result.mPenetrationAxis));
        out.startPenetrating = result.mFraction <= 0.0f;
        out.entity           = fromUserData(m_Impl->physics->GetBodyInterface().GetUserData(result.mBodyID2));
        return out;
    }

    std::vector<entt::entity> PhysicsSystem::collectOverlap(const JPH::Shape* shape,
                                                            const glm::vec3&  center,
                                                            const PhysicsQueryFilter& filter) const
    {
        std::vector<entt::entity> result;
        if (!m_Impl || !m_Impl->physics || !m_WorldService || !shape)
            return result;

        auto& world = m_WorldService->world();

        JPH::CollideShapeSettings settings;
        JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
        const MaskLayerFilter  objectFilter {filter.layerMask};
        const EntityBodyFilter bodyFilter {world, filter.activeOnly, filter.ignore};

        m_Impl->physics->GetNarrowPhaseQuery().CollideShape(shape,
                                                            JPH::Vec3::sOne(),
                                                            JPH::RMat44::sTranslation(toJoltR(center)),
                                                            settings,
                                                            toJoltR(center),
                                                            collector,
                                                            {},
                                                            objectFilter,
                                                            bodyFilter);

        auto& bodyInterface = m_Impl->physics->GetBodyInterface();
        std::unordered_set<entt::entity> seen;
        for (const auto& hit : collector.mHits)
        {
            const entt::entity entity = fromUserData(bodyInterface.GetUserData(hit.mBodyID2));
            if (seen.insert(entity).second)
                result.push_back(entity);
        }
        return result;
    }

    std::vector<entt::entity> PhysicsSystem::overlapSphere(const glm::vec3&          center,
                                                           float                     radius,
                                                           const PhysicsQueryFilter& filter) const
    {
        if (radius <= 0.0f)
            return {};
        auto shapeResult = JPH::SphereShapeSettings(radius).Create();
        if (shapeResult.HasError())
            return {};
        return collectOverlap(shapeResult.Get().GetPtr(), center, filter);
    }

    std::vector<entt::entity> PhysicsSystem::overlapBox(const glm::vec3&          center,
                                                        const glm::vec3&          halfExtents,
                                                        const PhysicsQueryFilter& filter) const
    {
        const glm::vec3 he = glm::max(glm::abs(halfExtents), glm::vec3 {0.001f});
        auto shapeResult = JPH::BoxShapeSettings(toJolt(he)).Create();
        if (shapeResult.HasError())
            return {};
        return collectOverlap(shapeResult.Get().GetPtr(), center, filter);
    }

    std::vector<entt::entity> PhysicsSystem::overlapCapsule(const glm::vec3&          center,
                                                            float                     halfHeight,
                                                            float                     radius,
                                                            const PhysicsQueryFilter& filter) const
    {
        if (radius <= 0.0f)
            return {};
        auto shapeResult = JPH::CapsuleShapeSettings(std::max(halfHeight, 0.001f), radius).Create();
        if (shapeResult.HasError())
            return {};
        return collectOverlap(shapeResult.Get().GetPtr(), center, filter);
    }

    std::vector<PhysicsContactPair> PhysicsSystem::contactPairs(bool activeOnly) const
    {
        std::vector<PhysicsContactPair> result;
        if (!m_WorldService)
            return result;

        auto& world = m_WorldService->world();
        auto& reg = world.registry();
        auto view = reg.view<TransformComponent, RigidBodyComponent>();
        std::vector<entt::entity> entities;
        for (auto entity : view)
        {
            if (!activeOnly || entityActive(world, entity))
                entities.push_back(entity);
        }

        for (size_t i = 0; i < entities.size(); ++i)
        {
            const auto a = entities[i];
            const auto* aTransform = reg.try_get<TransformComponent>(a);
            if (!aTransform)
                continue;
            const auto aHalfExtents = entityHalfExtents(world, a);
            for (size_t j = i + 1; j < entities.size(); ++j)
            {
                const auto b = entities[j];
                const auto* bTransform = reg.try_get<TransformComponent>(b);
                if (!bTransform)
                    continue;
                if (aabbIntersectsAabb(aTransform->position, aHalfExtents, bTransform->position, entityHalfExtents(world, b)))
                    result.push_back(PhysicsContactPair {.a = a, .b = b});
            }
        }
        return result;
    }

    std::vector<PhysicsContactEvent> PhysicsSystem::consumeContactEvents()
    {
        std::vector<PhysicsContactEvent> out;
        if (!m_Impl)
            return out;
        {
            std::scoped_lock lock(m_Impl->contactMutex);
            out.swap(m_Impl->contactEvents);
        }
        // Resolve the sensor flag on the main thread from live components.
        if (m_WorldService)
        {
            auto& reg = m_WorldService->world().registry();
            const auto isSensor = [&](entt::entity e) {
                if (e == entt::null || !reg.valid(e))
                    return false;
                const auto* rb = reg.try_get<RigidBodyComponent>(e);
                return rb && rb->isSensor;
            };
            for (auto& event : out)
                event.isSensor = isSensor(event.a) || isSensor(event.b);
        }
        return out;
    }

    bool PhysicsSystem::hasCharacter(entt::entity entity) const
    {
        return m_Impl && m_Impl->characters.contains(entity);
    }

    bool PhysicsSystem::characterMove(entt::entity entity, const glm::vec3& horizontalVelocity)
    {
        if (!m_WorldService)
            return false;
        auto* cc = m_WorldService->world().registry().try_get<CharacterControllerComponent>(entity);
        if (!cc)
            return false;
        cc->inputMove = {horizontalVelocity.x, 0.0f, horizontalVelocity.z};
        return true;
    }

    bool PhysicsSystem::characterJump(entt::entity entity, float speed)
    {
        if (!m_WorldService)
            return false;
        auto* cc = m_WorldService->world().registry().try_get<CharacterControllerComponent>(entity);
        if (!cc)
            return false;
        cc->jumpRequested = true;
        if (speed > 0.0f)
            cc->jumpSpeed = speed;
        return true;
    }

    bool PhysicsSystem::characterIsGrounded(entt::entity entity) const
    {
        if (m_Impl)
        {
            const auto it = m_Impl->characters.find(entity);
            if (it != m_Impl->characters.end() && it->second.character)
                return it->second.character->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
        }
        if (m_WorldService)
        {
            if (auto* cc = m_WorldService->world().registry().try_get<CharacterControllerComponent>(entity))
                return cc->grounded;
        }
        return false;
    }

    glm::vec3 PhysicsSystem::characterVelocity(entt::entity entity) const
    {
        if (m_Impl)
        {
            const auto it = m_Impl->characters.find(entity);
            if (it != m_Impl->characters.end() && it->second.character)
                return fromJolt(it->second.character->GetLinearVelocity());
        }
        if (m_WorldService)
        {
            if (auto* cc = m_WorldService->world().registry().try_get<CharacterControllerComponent>(entity))
                return cc->velocity;
        }
        return {};
    }

    glm::vec3 PhysicsSystem::characterGroundNormal(entt::entity entity) const
    {
        if (m_Impl)
        {
            const auto it = m_Impl->characters.find(entity);
            if (it != m_Impl->characters.end() && it->second.character)
                return fromJolt(it->second.character->GetGroundNormal());
        }
        return {0.0f, 1.0f, 0.0f};
    }

    bool PhysicsSystem::characterSetPosition(entt::entity entity, const glm::vec3& position)
    {
        if (!m_WorldService)
            return false;
        auto& reg = m_WorldService->world().registry();
        auto* transform = reg.try_get<TransformComponent>(entity);
        if (!transform)
            return false;
        transform->position = position;
        transform->dirty = true;
        if (m_Impl)
        {
            const auto it = m_Impl->characters.find(entity);
            if (it != m_Impl->characters.end() && it->second.character)
                it->second.character->SetPosition(toJoltR(position));
        }
        return true;
    }

    void PhysicsSystem::onPhysics(fsec dt)
    {
        if (!m_Impl || !m_Impl->physics || !m_WorldService)
            return;

        if (!m_Enabled || !m_Playing)
        {
            clearBodies();
            return;
        }

        syncWorldBodies();
        syncCharacters();

        if (m_Paused)
        {
            if (m_PendingSingleSteps == 0)
            {
                removeStaleBodies();
                return;
            }

            stepSimulation(m_FixedTimeStep);
            --m_PendingSingleSteps;
            m_Accumulator = 0.0f;
            syncDynamicBodiesToWorld();
            removeStaleBodies();
            return;
        }

        const float stepDt = m_TimingService ? m_TimingService->fixedDeltaTime() : m_FixedTimeStep;
        if (stepDt <= 0.0f)
            return;

        uint32_t steps = 0;
        if (m_TimingService)
        {
            steps = m_TimingService->fixedStepsThisFrame();
        }
        else
        {
            m_Accumulator += std::max(0.0f, dt.count());
            while (m_Accumulator >= stepDt && steps < m_FallbackMaxSubSteps)
            {
                m_Accumulator -= stepDt;
                ++steps;
            }
            if (steps == m_FallbackMaxSubSteps)
                m_Accumulator = std::min(m_Accumulator, stepDt);
        }

        for (uint32_t step = 0; step < steps; ++step)
            stepSimulation(stepDt);

        syncDynamicBodiesToWorld();
        removeStaleBodies();
    }

    void PhysicsSystem::syncWorldBodies()
    {
        auto& reg = m_WorldService->world().registry();
        auto  view = reg.view<RigidBodyComponent, TransformComponent>();
        for (auto entity : view)
            (void)ensureBody(entity);
    }

    void PhysicsSystem::stepSimulation(float seconds)
    {
        m_Impl->physics->Update(seconds, 1, m_Impl->tempAllocator.get(), m_Impl->jobSystem.get());
        updateCharacters(seconds);
    }

    void PhysicsSystem::syncDynamicBodiesToWorld()
    {
        auto& reg = m_WorldService->world().registry();
        auto& bodyInterface = m_Impl->physics->GetBodyInterface();
        for (auto& [entity, record] : m_Impl->bodies)
        {
            if (!reg.valid(entity))
                continue;

            auto* rb = reg.try_get<RigidBodyComponent>(entity);
            auto* transform = reg.try_get<TransformComponent>(entity);
            if (!rb || !transform || rb->motionType != 2u)
                continue;

            transform->position = fromJolt(bodyInterface.GetPosition(record.id));
            transform->rotation = fromJolt(bodyInterface.GetRotation(record.id));
            transform->dirty = true;
            rb->linearVelocity = fromJolt(bodyInterface.GetLinearVelocity(record.id));
            rb->angularVelocity = fromJolt(bodyInterface.GetAngularVelocity(record.id));
        }
    }

    void PhysicsSystem::removeStaleBodies()
    {
        auto& reg = m_WorldService->world().registry();
        for (auto it = m_Impl->bodies.begin(); it != m_Impl->bodies.end();)
        {
            if (!reg.valid(it->first) || !reg.all_of<RigidBodyComponent, TransformComponent>(it->first))
            {
                auto& bodyInterface = m_Impl->physics->GetBodyInterface();
                bodyInterface.RemoveBody(it->second.id);
                bodyInterface.DestroyBody(it->second.id);
                m_Impl->bodyToEntity.erase(it->second.id.GetIndexAndSequenceNumber());
                it = m_Impl->bodies.erase(it);
            }
            else
            {
                ++it;
            }
        }
        removeStaleCharacters();
    }

    void PhysicsSystem::clearBodies()
    {
        clearCharacters();

        if (!m_Impl || !m_Impl->physics || m_Impl->bodies.empty())
            return;

        auto& bodyInterface = m_Impl->physics->GetBodyInterface();
        for (auto& [entity, record] : m_Impl->bodies)
        {
            (void)entity;
            bodyInterface.RemoveBody(record.id);
            bodyInterface.DestroyBody(record.id);
        }
        m_Impl->bodies.clear();
        m_Impl->bodyToEntity.clear();
        {
            std::scoped_lock lock(m_Impl->contactMutex);
            m_Impl->contactEvents.clear();
        }
    }

    void PhysicsSystem::syncCharacters()
    {
        auto& reg = m_WorldService->world().registry();
        auto  view = reg.view<CharacterControllerComponent, TransformComponent>();
        for (auto entity : view)
            (void)ensureCharacter(entity);
    }

    bool PhysicsSystem::ensureCharacter(entt::entity entity)
    {
        auto& reg = m_WorldService->world().registry();
        auto* cc = reg.try_get<CharacterControllerComponent>(entity);
        auto* transform = reg.try_get<TransformComponent>(entity);
        if (!cc || !transform)
        {
            m_Impl->characters.erase(entity);
            return false;
        }

        const float radius = std::max(cc->radius, 0.05f);
        const float height = std::max(cc->height, 2.0f * radius + 0.02f);

        auto it = m_Impl->characters.find(entity);
        if (it != m_Impl->characters.end())
        {
            const auto& rec = it->second;
            if (rec.radius == radius && rec.height == height &&
                rec.maxSlopeAngleDegrees == cc->maxSlopeAngleDegrees && rec.objectLayer == cc->objectLayer)
                return true;
            m_Impl->characters.erase(it);
        }

        const float cylHalfHeight = (height - 2.0f * radius) * 0.5f;
        auto capsule = JPH::CapsuleShapeSettings(cylHalfHeight, radius).Create();
        if (capsule.HasError())
            return false;

        // Offset the capsule up so the character's origin is at the feet.
        auto shapeResult = JPH::RotatedTranslatedShapeSettings(
                               JPH::Vec3(0.0f, cylHalfHeight + radius, 0.0f), JPH::Quat::sIdentity(), capsule.Get())
                               .Create();
        if (shapeResult.HasError())
            return false;

        JPH::CharacterVirtualSettings settings;
        settings.mShape = shapeResult.Get();
        settings.mMaxSlopeAngle = glm::radians(cc->maxSlopeAngleDegrees);
        settings.mUp = JPH::Vec3::sAxisY();

        CharacterRecord rec;
        rec.character = new JPH::CharacterVirtual(
            &settings, toJoltR(transform->position), JPH::Quat::sIdentity(), toUserData(entity), m_Impl->physics.get());
        rec.radius = radius;
        rec.height = height;
        rec.maxSlopeAngleDegrees = cc->maxSlopeAngleDegrees;
        rec.objectLayer = cc->objectLayer;
        m_Impl->characters.emplace(entity, std::move(rec));
        return true;
    }

    void PhysicsSystem::updateCharacters(float seconds)
    {
        if (m_Impl->characters.empty())
            return;

        auto& world = m_WorldService->world();
        auto& reg = world.registry();
        const JPH::Vec3 gravity = toJolt(m_Gravity);

        for (auto& [entity, rec] : m_Impl->characters)
        {
            if (!reg.valid(entity) || !rec.character)
                continue;
            auto* cc = reg.try_get<CharacterControllerComponent>(entity);
            auto* transform = reg.try_get<TransformComponent>(entity);
            if (!cc || !transform || !entityActive(world, entity))
                continue;

            auto* ch = rec.character.GetPtr();
            const bool grounded = ch->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;

            JPH::Vec3 v = ch->GetLinearVelocity();
            v.SetX(cc->inputMove.x);
            v.SetZ(cc->inputMove.z);
            if (grounded)
            {
                if (v.GetY() < 0.0f)
                    v.SetY(0.0f);
                if (cc->jumpRequested)
                    v.SetY(cc->jumpSpeed);
            }
            v += gravity * (cc->gravityFactor * seconds);
            ch->SetLinearVelocity(v);

            JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
            updateSettings.mWalkStairsStepUp = JPH::Vec3(0.0f, std::max(cc->stepHeight, 0.0f), 0.0f);

            const JPH::ObjectLayer layer = encodeObjectLayer(cc->objectLayer, true);
            const JPH::DefaultBroadPhaseLayerFilter bpFilter(m_Impl->objectVsBroadPhaseLayerFilter, layer);
            const JPH::DefaultObjectLayerFilter     objFilter(m_Impl->objectLayerPairFilter, layer);

            ch->ExtendedUpdate(seconds,
                               gravity * cc->gravityFactor,
                               updateSettings,
                               bpFilter,
                               objFilter,
                               {},
                               {},
                               *m_Impl->tempAllocator);

            transform->position = fromJoltR(ch->GetPosition());
            transform->dirty = true;
            cc->velocity = fromJolt(ch->GetLinearVelocity());
            cc->grounded = ch->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
            cc->jumpRequested = false;
        }
    }

    void PhysicsSystem::removeStaleCharacters()
    {
        if (!m_Impl)
            return;
        auto& reg = m_WorldService->world().registry();
        for (auto it = m_Impl->characters.begin(); it != m_Impl->characters.end();)
        {
            if (!reg.valid(it->first) || !reg.all_of<CharacterControllerComponent, TransformComponent>(it->first))
                it = m_Impl->characters.erase(it);
            else
                ++it;
        }
    }

    void PhysicsSystem::clearCharacters()
    {
        if (m_Impl)
            m_Impl->characters.clear();
    }

    bool PhysicsSystem::buildSignature(entt::entity entity, BodySignature& out) const
    {
        auto& reg = m_WorldService->world().registry();
        const auto* rb = reg.try_get<RigidBodyComponent>(entity);
        if (!rb)
            return false;

        out.motionType = rb->motionType;
        out.objectLayer = rb->objectLayer;
        out.isSensor = rb->isSensor;
        out.motionQuality = rb->motionQuality;
        out.allowSleeping = rb->allowSleeping;
        out.friction = rb->friction;
        out.restitution = rb->restitution;
        out.linearDamping = rb->linearDamping;
        out.angularDamping = rb->angularDamping;
        out.gravityFactor = rb->gravityFactor;
        out.overrideMass = rb->overrideMass;
        out.mass = rb->mass;
        out.maxLinearVelocity = rb->maxLinearVelocity;
        out.maxAngularVelocity = rb->maxAngularVelocity;

        if (auto* box = reg.try_get<BoxShapeComponent>(entity))
        {
            out.shapeType = 1;
            out.shapeA = glm::max(box->halfExtents, glm::vec3 {0.001f});
            return true;
        }
        if (auto* sphere = reg.try_get<SphereShapeComponent>(entity))
        {
            out.shapeType = 2;
            out.shapeA.x = std::max(sphere->radius, 0.001f);
            return true;
        }
        if (auto* capsule = reg.try_get<CapsuleShapeComponent>(entity))
        {
            out.shapeType = 3;
            out.shapeA.x = std::max(capsule->halfHeightOfCylinder, 0.001f);
            out.shapeB = std::max(capsule->radius, 0.001f);
            return true;
        }
        if (auto* cylinder = reg.try_get<CylinderShapeComponent>(entity))
        {
            out.shapeType = 4;
            out.shapeA.x = std::max(cylinder->halfHeight, 0.01f);
            out.shapeB = std::max(cylinder->radius, 0.01f);
            return true;
        }
        if (auto* meshShape = reg.try_get<MeshShapeComponent>(entity))
        {
            const auto* mesh = reg.try_get<MeshComponent>(entity);
            if (mesh && mesh->mesh.valid())
            {
                const auto* transform = reg.try_get<TransformComponent>(entity);
                out.shapeType = 5;
                out.meshKey = static_cast<uint64_t>(std::hash<CoreUUID> {}(mesh->mesh));
                out.meshConvex = meshShape->convex ? 1u : 0u;
                out.meshScale = transform ? transform->scale : glm::vec3 {1.0f};
                return true;
            }
        }
        return false;
    }

    bool PhysicsSystem::ensureBody(entt::entity entity)
    {
        BodySignature signature {};
        if (!buildSignature(entity, signature))
        {
            destroyBody(entity);
            return false;
        }

        auto it = m_Impl->bodies.find(entity);
        if (it != m_Impl->bodies.end() && it->second.signature == signature)
        {
            auto& reg = m_WorldService->world().registry();
            const auto& rb = reg.get<RigidBodyComponent>(entity);
            const auto& transform = reg.get<TransformComponent>(entity);
            if (rb.motionType != 2u)
            {
                auto& bodyInterface = m_Impl->physics->GetBodyInterface();
                bodyInterface.SetPositionAndRotation(
                    it->second.id, toJoltR(transform.position), toJolt(transform.rotation), JPH::EActivation::Activate);
                if (rb.motionType == 1u)
                {
                    bodyInterface.SetLinearAndAngularVelocity(
                        it->second.id, toJolt(rb.linearVelocity), toJolt(rb.angularVelocity));
                }
            }
            return true;
        }

        destroyBody(entity);

        auto& reg = m_WorldService->world().registry();
        const auto& rb = reg.get<RigidBodyComponent>(entity);
        const auto& transform = reg.get<TransformComponent>(entity);

        JPH::ShapeRefC shape;
        switch (signature.shapeType)
        {
            case 1:
            {
                JPH::BoxShapeSettings settings(toJolt(signature.shapeA));
                auto result = settings.Create();
                if (result.HasError())
                    return false;
                shape = result.Get();
                break;
            }
            case 2:
            {
                JPH::SphereShapeSettings settings(signature.shapeA.x);
                auto result = settings.Create();
                if (result.HasError())
                    return false;
                shape = result.Get();
                break;
            }
            case 3:
            {
                JPH::CapsuleShapeSettings settings(signature.shapeA.x, signature.shapeB);
                auto result = settings.Create();
                if (result.HasError())
                    return false;
                shape = result.Get();
                break;
            }
            case 4:
            {
                JPH::CylinderShapeSettings settings(signature.shapeA.x, signature.shapeB);
                auto result = settings.Create();
                if (result.HasError())
                    return false;
                shape = result.Get();
                break;
            }
            case 5:
            {
                // Asset service may initialize after physics; resolve it lazily.
                if (!m_AssetService)
                    m_AssetService = ctx().services.tryGet<IAssetService>();
                const auto* mesh = reg.try_get<MeshComponent>(entity);
                shape = mesh ? buildMeshCollisionShape(
                                   m_AssetService, mesh->mesh, signature.meshConvex != 0u, signature.meshScale) :
                               JPH::ShapeRefC {};
                if (!shape)
                    return false;
                break;
            }
            default:
                return false;
        }

        const bool moving = rb.motionType != 0u;
        JPH::BodyCreationSettings settings(shape,
                                           toJoltR(transform.position),
                                           toJolt(transform.rotation),
                                           toMotionType(rb.motionType),
                                           encodeObjectLayer(rb.objectLayer, moving));
        settings.mUserData = toUserData(entity);
        settings.mIsSensor = rb.isSensor;
        settings.mMotionQuality = toMotionQuality(rb.motionQuality);
        settings.mAllowSleeping = rb.allowSleeping;
        settings.mFriction = rb.friction;
        settings.mRestitution = rb.restitution;
        settings.mLinearDamping = rb.linearDamping;
        settings.mAngularDamping = rb.angularDamping;
        settings.mGravityFactor = rb.gravityFactor;
        settings.mMaxLinearVelocity = rb.maxLinearVelocity;
        settings.mMaxAngularVelocity = rb.maxAngularVelocity;
        if (rb.overrideMass)
        {
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = std::max(rb.mass, 0.001f);
        }

        auto& bodyInterface = m_Impl->physics->GetBodyInterface();
        const JPH::BodyID id = bodyInterface.CreateAndAddBody(settings, JPH::EActivation::Activate);
        if (id.IsInvalid())
            return false;

        if (rb.motionType != 0u)
            bodyInterface.SetLinearAndAngularVelocity(id, toJolt(rb.linearVelocity), toJolt(rb.angularVelocity));

        m_Impl->bodies.emplace(entity, BodyRecord {.id = id, .signature = signature});
        m_Impl->bodyToEntity[id.GetIndexAndSequenceNumber()] = entity;
        return true;
    }

    void PhysicsSystem::destroyBody(entt::entity entity)
    {
        if (!m_Impl || !m_Impl->physics)
            return;

        auto it = m_Impl->bodies.find(entity);
        if (it == m_Impl->bodies.end())
            return;

        auto& bodyInterface = m_Impl->physics->GetBodyInterface();
        bodyInterface.RemoveBody(it->second.id);
        bodyInterface.DestroyBody(it->second.id);
        m_Impl->bodyToEntity.erase(it->second.id.GetIndexAndSequenceNumber());
        m_Impl->bodies.erase(it);
    }
} // namespace vultra
