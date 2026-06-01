#include "vultra/function/physics/physics_system.hpp"

#include "vultra/core/base/common_context.hpp"
#include "vultra/core/services/timing_service.hpp"
#include "vultra/function/services/job_service.hpp"
#include "vultra/function/services/world_service.hpp"
#include "vultra/function/world/components/box_shape_component.hpp"
#include "vultra/function/world/components/capsule_shape_component.hpp"
#include "vultra/function/world/components/entity_status_component.hpp"
#include "vultra/function/world/components/rigid_body_component.hpp"
#include "vultra/function/world/components/sphere_shape_component.hpp"
#include "vultra/function/world/components/transform_component.hpp"
#include "vultra/function/world/world.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemWithBarrier.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/MotionType.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

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
        constexpr JPH::ObjectLayer kObjectLayerNonMoving = 0;
        constexpr JPH::ObjectLayer kObjectLayerMoving    = 1;
        constexpr JPH::uint        kNumObjectLayers      = 2;

        constexpr JPH::BroadPhaseLayer kBroadPhaseNonMoving {0};
        constexpr JPH::BroadPhaseLayer kBroadPhaseMoving {1};
        constexpr JPH::uint            kNumBroadPhaseLayers = 2;

        std::atomic_uint32_t g_JoltUsers {0};

        JPH::Vec3 toJolt(const glm::vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
        JPH::RVec3 toJoltR(const glm::vec3& v) { return JPH::RVec3(v.x, v.y, v.z); }
        JPH::Quat toJolt(const glm::quat& q) { return JPH::Quat(q.x, q.y, q.z, q.w); }

        glm::vec3 fromJolt(const JPH::Vec3& v) { return {v.GetX(), v.GetY(), v.GetZ()}; }
        glm::quat fromJolt(const JPH::Quat& q) { return {q.GetW(), q.GetX(), q.GetY(), q.GetZ()}; }

        bool entityActive(World& world, entt::entity entity)
        {
            const auto* status = world.registry().try_get<EntityStatusComponent>(entity);
            return !status || status->active;
        }

        float maxComponent(const glm::vec3& v) { return std::max({v.x, v.y, v.z}); }

        bool sphereIntersectsAabb(const glm::vec3& center,
                                  float radius,
                                  const glm::vec3& boxCenter,
                                  const glm::vec3& halfExtents)
        {
            const glm::vec3 closest = glm::clamp(center, boxCenter - halfExtents, boxCenter + halfExtents);
            const glm::vec3 delta   = center - closest;
            return glm::dot(delta, delta) <= radius * radius;
        }

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

        bool overlapsSphere(World& world, entt::entity entity, const glm::vec3& center, float radius)
        {
            auto& reg = world.registry();
            const auto* transform = reg.try_get<TransformComponent>(entity);
            if (!transform)
                return false;

            const glm::vec3 scale = glm::abs(transform->scale);
            if (const auto* sphere = reg.try_get<SphereShapeComponent>(entity))
            {
                const float scaledRadius = sphere->radius * maxComponent(scale);
                const glm::vec3 delta    = transform->position - center;
                const float combined     = scaledRadius + radius;
                return glm::dot(delta, delta) <= combined * combined;
            }

            if (const auto* box = reg.try_get<BoxShapeComponent>(entity))
                return sphereIntersectsAabb(center, radius, transform->position, box->halfExtents * scale);

            if (const auto* capsule = reg.try_get<CapsuleShapeComponent>(entity))
            {
                const float scaledRadius = (capsule->halfHeightOfCylinder + capsule->radius) * maxComponent(scale);
                const glm::vec3 delta    = transform->position - center;
                const float combined     = scaledRadius + radius;
                return glm::dot(delta, delta) <= combined * combined;
            }

            return false;
        }

        bool overlapsBox(World& world, entt::entity entity, const glm::vec3& center, const glm::vec3& halfExtents)
        {
            auto& reg = world.registry();
            const auto* transform = reg.try_get<TransformComponent>(entity);
            if (!transform)
                return false;

            return aabbIntersectsAabb(center, halfExtents, transform->position, entityHalfExtents(world, entity));
        }

        std::optional<PhysicsRaycastHit> raycastAabb(entt::entity entity,
                                                     const glm::vec3& origin,
                                                     const glm::vec3& direction,
                                                     float maxDistance,
                                                     const glm::vec3& center,
                                                     const glm::vec3& halfExtents)
        {
            float tMin = 0.0f;
            float tMax = maxDistance;
            glm::vec3 hitNormal {0.0f, 1.0f, 0.0f};

            for (int axis = 0; axis < 3; ++axis)
            {
                const float o = origin[axis];
                const float d = direction[axis];
                const float minB = center[axis] - halfExtents[axis];
                const float maxB = center[axis] + halfExtents[axis];

                if (std::abs(d) < 0.000001f)
                {
                    if (o < minB || o > maxB)
                        return std::nullopt;
                    continue;
                }

                float t1 = (minB - o) / d;
                float t2 = (maxB - o) / d;
                float normalSign = -1.0f;
                if (t1 > t2)
                {
                    std::swap(t1, t2);
                    normalSign = 1.0f;
                }

                if (t1 > tMin)
                {
                    tMin = t1;
                    hitNormal = {};
                    hitNormal[axis] = normalSign;
                }
                tMax = std::min(tMax, t2);
                if (tMin > tMax)
                    return std::nullopt;
            }

            if (tMin < 0.0f || tMin > maxDistance)
                return std::nullopt;

            return PhysicsRaycastHit {
                .entity = entity,
                .point = origin + direction * tMin,
                .normal = hitNormal,
                .fraction = maxDistance > 0.0f ? tMin / maxDistance : 0.0f,
                .distance = tMin,
            };
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

        JPH::ObjectLayer toObjectLayer(uint32_t value)
        {
            return value == 0u ? kObjectLayerNonMoving : kObjectLayerMoving;
        }

        class BroadPhaseLayerInterface final : public JPH::BroadPhaseLayerInterface
        {
        public:
            BroadPhaseLayerInterface()
            {
                m_ObjectToBroadPhase[kObjectLayerNonMoving] = kBroadPhaseNonMoving;
                m_ObjectToBroadPhase[kObjectLayerMoving]    = kBroadPhaseMoving;
            }

            JPH::uint GetNumBroadPhaseLayers() const override { return kNumBroadPhaseLayers; }

            JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
            {
                return m_ObjectToBroadPhase[std::min<JPH::ObjectLayer>(layer, kObjectLayerMoving)];
            }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
            const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
            {
                return layer == kBroadPhaseNonMoving ? "NonMoving" : "Moving";
            }
#endif

        private:
            std::array<JPH::BroadPhaseLayer, kNumObjectLayers> m_ObjectToBroadPhase {};
        };

        class ObjectVsBroadPhaseLayerFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
        {
        public:
            bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhaseLayer) const override
            {
                if (layer == kObjectLayerNonMoving)
                    return broadPhaseLayer == kBroadPhaseMoving;
                return true;
            }
        };

        class ObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter
        {
        public:
            bool ShouldCollide(JPH::ObjectLayer layer1, JPH::ObjectLayer layer2) const override
            {
                if (layer1 == kObjectLayerNonMoving && layer2 == kObjectLayerNonMoving)
                    return false;
                return true;
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

    struct PhysicsSystem::Impl
    {
        BroadPhaseLayerInterface broadPhaseLayerInterface;
        ObjectVsBroadPhaseLayerFilter objectVsBroadPhaseLayerFilter;
        ObjectLayerPairFilter objectLayerPairFilter;
        std::unique_ptr<JPH::TempAllocator> tempAllocator;
        std::unique_ptr<VTaskJoltJobSystem> jobSystem;
        std::unique_ptr<JPH::PhysicsSystem> physics;
        std::unordered_map<entt::entity, BodyRecord> bodies;
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
        m_Impl->physics->Init(kMaxBodies,
                              kNumBodyMutexes,
                              kMaxBodyPairs,
                              kMaxContactConstraints,
                              m_Impl->broadPhaseLayerInterface,
                              m_Impl->objectVsBroadPhaseLayerFilter,
                              m_Impl->objectLayerPairFilter);

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

    std::optional<PhysicsRaycastHit> PhysicsSystem::raycast(const glm::vec3& origin,
                                                            const glm::vec3& direction,
                                                            float            maxDistance,
                                                            bool             activeOnly) const
    {
        if (!m_WorldService || maxDistance <= 0.0f)
            return std::nullopt;

        const float dirLenSq = glm::dot(direction, direction);
        if (dirLenSq <= 0.000001f)
            return std::nullopt;

        const glm::vec3 dir = direction / std::sqrt(dirLenSq);
        auto&           world = m_WorldService->world();
        auto&           reg = world.registry();
        auto            view = reg.view<TransformComponent, RigidBodyComponent>();

        std::optional<PhysicsRaycastHit> best;
        for (auto entity : view)
        {
            if (activeOnly && !entityActive(world, entity))
                continue;

            const auto* transform = reg.try_get<TransformComponent>(entity);
            if (!transform)
                continue;

            const auto hit = raycastAabb(entity, origin, dir, maxDistance, transform->position, entityHalfExtents(world, entity));
            if (!hit)
                continue;
            if (!best || hit->distance < best->distance)
                best = hit;
        }

        return best;
    }

    std::vector<entt::entity> PhysicsSystem::overlapSphere(const glm::vec3& center,
                                                           float            radius,
                                                           bool             activeOnly) const
    {
        std::vector<entt::entity> result;
        if (!m_WorldService || radius < 0.0f)
            return result;

        auto& world = m_WorldService->world();
        auto& reg   = world.registry();
        auto  view  = reg.view<TransformComponent, RigidBodyComponent>();

        const float queryRadius = std::max(0.0f, radius);
        for (auto entity : view)
        {
            if (activeOnly && !entityActive(world, entity))
                continue;
            if (overlapsSphere(world, entity, center, queryRadius))
                result.push_back(entity);
        }
        return result;
    }

    std::vector<entt::entity> PhysicsSystem::overlapBox(const glm::vec3& center,
                                                        const glm::vec3& halfExtents,
                                                        bool             activeOnly) const
    {
        std::vector<entt::entity> result;
        if (!m_WorldService)
            return result;

        const glm::vec3 queryHalfExtents = glm::max(glm::abs(halfExtents), glm::vec3 {0.0f});
        auto& world = m_WorldService->world();
        auto& reg = world.registry();
        auto view = reg.view<TransformComponent, RigidBodyComponent>();

        for (auto entity : view)
        {
            if (activeOnly && !entityActive(world, entity))
                continue;
            if (overlapsBox(world, entity, center, queryHalfExtents))
                result.push_back(entity);
        }
        return result;
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
                it = m_Impl->bodies.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void PhysicsSystem::clearBodies()
    {
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
            default:
                return false;
        }

        JPH::BodyCreationSettings settings(
            shape, toJoltR(transform.position), toJolt(transform.rotation), toMotionType(rb.motionType), toObjectLayer(rb.objectLayer));
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
        m_Impl->bodies.erase(it);
    }
} // namespace vultra
