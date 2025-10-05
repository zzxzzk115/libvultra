#include "vultra/function/physics/physics_system.hpp"
#include "vultra/core/base/common_context.hpp"

// The Jolt headers don't include Jolt.h. Always include Jolt.h before including any other Jolt header.
#include <Jolt/Jolt.h>

// Jolt includes
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <cstdarg>

// Disable common warnings triggered by Jolt
JPH_SUPPRESS_WARNINGS

using namespace JPH;
using namespace JPH::literals;

void traceImpl(const char* inFMT, ...)
{
    // Format the message
    va_list list;
    va_start(list, inFMT);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), inFMT, list);
    va_end(list);

    VULTRA_CORE_TRACE("[Physics] {}", buffer);
}

#ifdef JPH_ENABLE_ASSERTS
bool assertFailedImpl(const char* inExpression, const char* inMessage, const char* inFile, uint inLine)
{
    // Print to the TTY
    cout << inFile << ":" << inLine << ": (" << inExpression << ") " << (inMessage != nullptr ? inMessage : "") << endl;

    // Breakpoint
    return true;
};
#endif // JPH_ENABLE_ASSERTS

// Layer that objects can be in, determines which other objects it can collide with
// Typically you at least want to have 1 layer for moving bodies and 1 layer for static bodies, but you can have
// more layers if you want. E.g. you could have a layer for high detail collision (which is not used by the physics
// simulation but only if you do collision testing).
namespace Layers
{
    static constexpr ObjectLayer NON_MOVING = 0;
    static constexpr ObjectLayer MOVING     = 1;
    static constexpr ObjectLayer NUM_LAYERS = 2;
}; // namespace Layers

/// Class that determines if two object layers can collide
class ObjectLayerPairFilterImpl : public ObjectLayerPairFilter
{
public:
    virtual bool ShouldCollide(ObjectLayer inObject1, ObjectLayer inObject2) const override
    {
        switch (inObject1)
        {
            case Layers::NON_MOVING:
                return inObject2 == Layers::MOVING; // Non moving only collides with moving
            case Layers::MOVING:
                return true; // Moving collides with everything
            default:
                JPH_ASSERT(false);
                return false;
        }
    }
};

// Each broadphase layer results in a separate bounding volume tree in the broad phase. You at least want to have
// a layer for non-moving and moving objects to avoid having to update a tree full of static objects every frame.
// You can have a 1-on-1 mapping between object layers and broadphase layers (like in this case) but if you have
// many object layers you'll be creating many broad phase trees, which is not efficient. If you want to fine tune
// your broadphase layers define JPH_TRACK_BROADPHASE_STATS and look at the stats reported on the TTY.
namespace BroadPhaseLayers
{
    static constexpr BroadPhaseLayer NON_MOVING(0);
    static constexpr BroadPhaseLayer MOVING(1);
    static constexpr uint            NUM_LAYERS(2);
}; // namespace BroadPhaseLayers

// BroadPhaseLayerInterface implementation
// This defines a mapping between object and broadphase layers.
class BPLayerInterfaceImpl final : public BroadPhaseLayerInterface
{
public:
    BPLayerInterfaceImpl()
    {
        // Create a mapping table from object to broad phase layer
        m_ObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
        m_ObjectToBroadPhase[Layers::MOVING]     = BroadPhaseLayers::MOVING;
    }

    virtual uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NUM_LAYERS; }

    virtual BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer inLayer) const override
    {
        JPH_ASSERT(inLayer < Layers::NUM_LAYERS);
        return m_ObjectToBroadPhase[inLayer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    virtual const char* GetBroadPhaseLayerName(BroadPhaseLayer inLayer) const override
    {
        switch (static_cast<BroadPhaseLayer::Type>(inLayer))
        {
            case static_cast<BroadPhaseLayer::Type>(BroadPhaseLayers::NON_MOVING):
                return "NON_MOVING";
            case static_cast<BroadPhaseLayer::Type>(BroadPhaseLayers::MOVING):
                return "MOVING";
            default:
                JPH_ASSERT(false);
                return "INVALID";
        }
    }
#endif // JPH_EXTERNAL_PROFILE || JPH_PROFILE_ENABLED

private:
    BroadPhaseLayer m_ObjectToBroadPhase[Layers::NUM_LAYERS];
};

/// Class that determines if an object layer can collide with a broadphase layer
class ObjectVsBroadPhaseLayerFilterImpl : public ObjectVsBroadPhaseLayerFilter
{
public:
    virtual bool ShouldCollide(ObjectLayer inLayer1, BroadPhaseLayer inLayer2) const override
    {
        switch (inLayer1)
        {
            case Layers::NON_MOVING:
                return inLayer2 == BroadPhaseLayers::MOVING;
            case Layers::MOVING:
                return true;
            default:
                JPH_ASSERT(false);
                return false;
        }
    }
};

// An example contact listener
class MyContactListener : public ContactListener
{
public:
    // See: ContactListener
    virtual ValidateResult OnContactValidate(const Body&               inBody1,
                                             const Body&               inBody2,
                                             RVec3Arg                  inBaseOffset,
                                             const CollideShapeResult& inCollisionResult) override
    {
        VULTRA_CORE_TRACE("[Physics] Validating contact between body {} and body {}",
                          inBody1.GetID().GetIndexAndSequenceNumber(),
                          inBody2.GetID().GetIndexAndSequenceNumber());
        return ValidateResult::AcceptAllContactsForThisBodyPair;
    }

    virtual void OnContactAdded(const Body&            inBody1,
                                const Body&            inBody2,
                                const ContactManifold& inManifold,
                                ContactSettings&       ioSettings) override
    {
        VULTRA_CORE_TRACE("[Physics] A contact was added between body {} and body {}",
                          inBody1.GetID().GetIndexAndSequenceNumber(),
                          inBody2.GetID().GetIndexAndSequenceNumber());
    }

    virtual void OnContactPersisted(const Body&            inBody1,
                                    const Body&            inBody2,
                                    const ContactManifold& inManifold,
                                    ContactSettings&       ioSettings) override
    {
        VULTRA_CORE_TRACE("[Physics] A contact was persisted between body {} and body {}",
                          inBody1.GetID().GetIndexAndSequenceNumber(),
                          inBody2.GetID().GetIndexAndSequenceNumber());
    }

    virtual void OnContactRemoved(const SubShapeIDPair& inSubShapePair) override
    {
        VULTRA_CORE_TRACE("[Physics] A contact was removed between bodies {} and {}",
                          inSubShapePair.GetBody1ID().GetIndexAndSequenceNumber(),
                          inSubShapePair.GetBody2ID().GetIndexAndSequenceNumber());
    }
};

// An example activation listener
class MyBodyActivationListener : public BodyActivationListener
{
public:
    virtual void OnBodyActivated(const BodyID& inBodyID, uint64 inBodyUserData) override
    {
        VULTRA_CORE_TRACE("[Physics] A body {} got activated", inBodyID.GetIndexAndSequenceNumber());
    }

    virtual void OnBodyDeactivated(const BodyID& inBodyID, uint64 inBodyUserData) override
    {
        VULTRA_CORE_TRACE("[Physics] A body {} went to sleep", inBodyID.GetIndexAndSequenceNumber());
    }
};

namespace vultra
{
    namespace physics
    {
        PhysicsSystem::PhysicsSystem()
        {
            RegisterDefaultAllocator();

            Trace = traceImpl;
            JPH_IF_ENABLE_ASSERTS(AssertFailed = assertFailedImpl;)

            Factory::sInstance = new Factory();

            RegisterTypes();

            m_TempAllocator = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);

            m_JobSystem = std::make_unique<JPH::JobSystemThreadPool>(
                cMaxPhysicsJobs, cMaxPhysicsBarriers, thread::hardware_concurrency() - 1);

            const uint cMaxBodies = 1024;

            const uint cNumBodyMutexes = 0;

            const uint cMaxBodyPairs = 1024;

            const uint cMaxContactConstraints = 1024;

            BPLayerInterfaceImpl broadPhaseLayerInterface {};

            ObjectVsBroadPhaseLayerFilterImpl objectVsBroadPhaseLayerFilter {};

            ObjectLayerPairFilterImpl objectLayerPairFilter {};

            m_JoltPhysicsSystem = std::make_unique<JPH::PhysicsSystem>();

            m_JoltPhysicsSystem->Init(cMaxBodies,
                                      cNumBodyMutexes,
                                      cMaxBodyPairs,
                                      cMaxContactConstraints,
                                      broadPhaseLayerInterface,
                                      objectVsBroadPhaseLayerFilter,
                                      objectLayerPairFilter);

            MyBodyActivationListener bodyActivationListener {};
            m_JoltPhysicsSystem->SetBodyActivationListener(&bodyActivationListener);

            // A contact listener gets notified when bodies (are about to) collide, and when they separate again.
            // Note that this is called from a job so whatever you do here needs to be thread safe.
            // Registering one is entirely optional.
            MyContactListener contact_listener;
            m_JoltPhysicsSystem->SetContactListener(&contact_listener);

            // The main way to interact with the bodies in the physics system is through the body interface. There is a
            // locking and a non-locking variant of this. We're going to use the locking version (even though we're not
            // planning to access bodies from multiple threads)
            m_BodyInterface = &m_JoltPhysicsSystem->GetBodyInterface();
        }

        PhysicsSystem::~PhysicsSystem()
        {
            // Unregisters all types with the factory and cleans up the default material
            UnregisterTypes();

            // Destroy the factory
            delete Factory::sInstance;
            Factory::sInstance = nullptr;
        }

        void PhysicsSystem::setScene(LogicScene* scene)
        {
            // TODO: Traverse the entities in the scene and create Jolt bodies for them
        }

        void PhysicsSystem::stepSimulation(float deltaTime)
        {
            // Step the simulation
            m_JoltPhysicsSystem->Update(deltaTime, 1, m_TempAllocator.get(), m_JobSystem.get());
        }
    } // namespace physics
} // namespace vultra
