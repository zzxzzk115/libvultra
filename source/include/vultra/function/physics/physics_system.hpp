#pragma once

#include <memory>

namespace JPH
{
    class PhysicsSystem;
    class TempAllocatorImpl;
    class JobSystemThreadPool;
    class BodyInterface;
} // namespace JPH

namespace vultra
{
    class LogicScene;

    namespace physics
    {
        class PhysicsSystem
        {
        public:
            PhysicsSystem();
            ~PhysicsSystem();

            void setScene(LogicScene* scene);

            void stepSimulation(float deltaTime);

        private:
            std::unique_ptr<JPH::PhysicsSystem>       m_JoltPhysicsSystem {nullptr};
            std::unique_ptr<JPH::TempAllocatorImpl>   m_TempAllocator {nullptr};
            std::unique_ptr<JPH::JobSystemThreadPool> m_JobSystem {nullptr};
            JPH::BodyInterface*                       m_BodyInterface {nullptr};
        };
    } // namespace physics
} // namespace vultra