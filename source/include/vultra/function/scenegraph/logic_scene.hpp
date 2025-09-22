#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/base/uuid.hpp"
#include "vultra/function/renderer/renderable.hpp"

#include <entt/entt.hpp>

#include <filesystem>
#include <unordered_map>

namespace vultra
{
    class Entity;

    enum class LogicSceneSimulationMode
    {
        eInvalid = 0,
        eGame,
        eEditor
    };

    enum class LogicSceneSimulationStatus
    {
        eInvalid = 0,
        eStopped,
        eSimulating,
        ePaused
    };

    class LogicScene
    {
    public:
        explicit LogicScene(const std::string& name = "Untitled Scene", bool copy = false);
        ~LogicScene() = default;

        static Ref<LogicScene> copy(const Ref<LogicScene>& other);

        void setSimulationMode(LogicSceneSimulationMode mode)
        {
            m_SimulationMode = mode;
            if (mode == LogicSceneSimulationMode::eEditor)
            {
                m_SimulationStatus = LogicSceneSimulationStatus::eStopped;
            }
        }
        LogicSceneSimulationMode getSimulationMode() const { return m_SimulationMode; }

        void setSimulationStatus(LogicSceneSimulationStatus status)
        {
            switch (m_SimulationStatus)
            {
                case LogicSceneSimulationStatus::eStopped:
                case LogicSceneSimulationStatus::ePaused:
                    if (status == LogicSceneSimulationStatus::eSimulating)
                    {
                        m_SimulationStatus = status;
                    }
                    break;

                case LogicSceneSimulationStatus::eSimulating:
                    if (status == LogicSceneSimulationStatus::eStopped || status == LogicSceneSimulationStatus::ePaused)
                    {
                        m_SimulationStatus = status;
                    }
                    break;

                case LogicSceneSimulationStatus::eInvalid:
                    break;
            }
        }
        LogicSceneSimulationStatus getSimulationStatus() const { return m_SimulationStatus; }

        void               setName(const std::string& name) { m_Name = name; }
        const std::string& getName() const { return m_Name; }

        std::filesystem::path getPath() const { return m_Path; }

        entt::registry& getRegistry() { return m_Registry; }

        Entity createEntity(const std::string& name = std::string());
        Entity createEntityFromContent(CoreUUID uuid, const std::string& name = std::string());
        void   destroyEntity(Entity entity);
        Entity getEntityWithCoreUUID(CoreUUID uuid) const;
        Entity getEntityWithName(const std::string& name);

        // Helpers to create common entities
        Entity              createMainCamera();
        Entity              getMainCamera() const;
        Entity              createXrCamera(bool leftEye);
        Entity              getXrCamera(bool leftEye) const;
        Entity              createDirectionalLight();
        Entity              createPointLight();
        Entity              createAreaLight();
        Entity              getDirectionalLight() const;
        Entity              getPointLight(uint32_t index) const;
        std::vector<Entity> getPointLights() const;
        Entity              getAreaLight(uint32_t index) const;
        std::vector<Entity> getAreaLights() const;
        Entity              createMeshEntity(const std::string& name, const std::string& meshPath);

        // Cook renderables for rendering
        std::vector<gfx::Renderable> cookRenderables();

        void onLoad();
        void onTick(float deltaTime);
        void onFixedTick();
        void onUnload();

        void saveTo(const std::filesystem::path& dstPath);
        void clearAndLoad();
        void loadFrom(const std::filesystem::path& srcPath);

        std::vector<Entity> getEntitiesSortedByName();

    private:
        void        createDefaultEntities();
        std::string getNameFromEntity(Entity entity) const;

        static int         extractEntityNumber(const std::string& name);
        static std::string extractEntityName(const std::string& name);

        void initAfterDeserializing();

        template<typename T>
        void onComponentAdded(Entity entity, T& component);

    private:
        std::string                               m_Name;
        entt::registry                            m_Registry;
        Ref<std::map<CoreUUID, Entity>>           m_EntityMap;
        std::unordered_map<std::string, uint32_t> m_Name2CountMap;
        std::filesystem::path                     m_Path;
        bool                                      m_IsLoaded = false;

        LogicSceneSimulationStatus m_SimulationStatus = LogicSceneSimulationStatus::eSimulating;
        LogicSceneSimulationMode   m_SimulationMode   = LogicSceneSimulationMode::eGame;

        friend class Entity;
    };
} // namespace vultra