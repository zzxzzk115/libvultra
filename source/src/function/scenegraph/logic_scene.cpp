#include "vultra/function/scenegraph/logic_scene.hpp"
#include "vultra/function/io/serialization.hpp"
#include "vultra/function/renderer/mesh_manager.hpp"
#include "vultra/function/scenegraph/components.hpp"
#include "vultra/function/scenegraph/entity.hpp"

#include <regex>

namespace vultra
{
#define ON_COMPONENT_ADDED(comp) \
    template<> \
    void LogicScene::onComponentAdded<comp>(Entity entity, comp & component)

    template<typename... Component>
    static void copyComponent(entt::registry& dst, entt::registry& src, Ref<std::map<CoreUUID, Entity>>& enttMap)
    {
        (
            [&]() {
                auto view = src.view<Component>();
                for (auto srcEntity : view)
                {
                    Entity dstEntity = enttMap->at(src.get<IDComponent>(srcEntity).Id);

                    auto& srcComponent = src.get<Component>(srcEntity);
                    dst.emplace_or_replace<Component>(dstEntity, srcComponent);
                }
            }(),
            ...);
    }

    template<typename... Component>
    static void copyComponent(ComponentGroup<Component...>,
                              entt::registry&                  dst,
                              entt::registry&                  src,
                              Ref<std::map<CoreUUID, Entity>>& enttMap)
    {
        copyComponent<Component...>(dst, src, enttMap);
    }

    template<typename... Component>
    static void copyComponentIfExists(Entity dst, Entity src)
    {
        (
            [&]() {
                if (src.hasComponent<Component>())
                    dst.addOrReplaceComponent<Component>(src.getComponent<Component>());
            }(),
            ...);
    }

    template<typename... Component>
    static void CopyComponentIfExists(ComponentGroup<Component...>, Entity dst, Entity src)
    {
        CopyComponentIfExists<Component...>(dst, src);
    }

    Ref<LogicScene> LogicScene::copy(const Ref<LogicScene>& other)
    {
        Ref<LogicScene> newScene     = createRef<LogicScene>(other->getName() + " (Copy)", true);
        newScene->m_SimulationMode   = other->m_SimulationMode;
        newScene->m_SimulationStatus = other->m_SimulationStatus;
        newScene->m_Name2CountMap    = other->m_Name2CountMap;

        auto& srcSceneRegistry = other->m_Registry;
        auto& dstSceneRegistry = newScene->m_Registry;

        // Create entities in new scene
        auto idView = srcSceneRegistry.view<IDComponent>();
        for (auto e : idView)
        {
            CoreUUID    uuid               = srcSceneRegistry.get<IDComponent>(e).id;
            const auto& name               = srcSceneRegistry.get<NameComponent>(e).name;
            Entity      newEntity          = newScene->createEntityFromContent(uuid, name);
            (*newScene->m_EntityMap)[uuid] = newEntity;
        }

        // Copy components (except IDComponent and NameComponent)
        copyComponent(AllCopyableComponents {}, dstSceneRegistry, srcSceneRegistry, newScene->m_EntityMap);

        return newScene;
    }

    LogicScene::LogicScene(const std::string& name, bool copy) : m_Name(name)
    {
        m_EntityMap = createRef<std::map<CoreUUID, Entity>>();

        if (!copy)
        {
            createDefaultEntities();
        }
    }

    Entity LogicScene::createEntity(const std::string& name)
    {
        CoreUUID uuid = CoreUUIDHelper::createStandardUUID();
        Entity   entity(m_Registry.create(), this);
        entity.addComponent<IDComponent>(uuid);

        // check duplicate
        auto     createName     = name.empty() ? "Entity" : name;
        uint32_t duplicateCount = m_Name2CountMap.count(createName) > 0 ? m_Name2CountMap[createName] : 0;
        if (duplicateCount > 0)
        {
            // assign a new name, avoid same name
            m_Name2CountMap[createName]++;
            createName = fmt::format("{0} ({1})", createName, duplicateCount);
        }
        else
        {
            m_Name2CountMap[createName] = 1;
        }
        entity.addComponent<NameComponent>(createName);

        (*m_EntityMap)[uuid] = entity;

        return entity;
    }

    Entity LogicScene::createEntityFromContent(CoreUUID uuid, const std::string& name)
    {
        Entity entity {m_Registry.create(), this};

        entity.addComponent<IDComponent>(uuid);
        entity.addComponent<NameComponent>(name);

        (*m_EntityMap)[uuid] = entity;

        return entity;
    }

    void LogicScene::destroyEntity(Entity entity)
    {
        // // Destroy children
        // if (entity.hasChildren())
        // {
        //     for (const auto& childUUID : entity.getChildrenUUIDs())
        //     {
        //         destroyEntity(getEntityWithCoreUUID(childUUID));
        //     }
        // }

        // // If self has parent, let parent remove self
        // if (entity.hasParent())
        // {
        //     auto parent = getEntityWithCoreUUID(entity.getParentUUID());
        //     parent.removeChild(entity.getCoreUUID());
        // }

        // Destroy self
        m_EntityMap->erase(entity.getCoreUUID());
        m_Registry.destroy(entity);
    }

    Entity LogicScene::getEntityWithCoreUUID(CoreUUID id) const { return m_EntityMap->at(id); }

    Entity LogicScene::getEntityWithName(const std::string& name)
    {
        auto view = m_Registry.view<NameComponent>();
        for (auto entity : view)
        {
            auto& nameComponent = view.get<NameComponent>(entity);
            if (nameComponent.name == name)
            {
                return {entity, this};
            }
        }

        return {};
    }

    Entity LogicScene::createMainCamera()
    {
        Entity cameraEntity = createEntity("Main Camera");
        cameraEntity.addComponent<TransformComponent>();
        cameraEntity.addComponent<CameraComponent>();
        return cameraEntity;
    }

    Entity LogicScene::getMainCamera() const
    {
        // Find the entity with CameraComponent and isPrimary == true
        auto view = m_Registry.view<CameraComponent>();
        for (auto entity : view)
        {
            const auto& cameraComponent = view.get<CameraComponent>(entity);
            if (cameraComponent.isPrimary)
            {
                return {entity, const_cast<LogicScene*>(this)};
            }
        }

        return {};
    }

    Entity LogicScene::createDirectionalLight()
    {
        Entity lightEntity = createEntity("Directional Light");
        lightEntity.addComponent<TransformComponent>();
        lightEntity.addComponent<DirectionalLightComponent>();
        return lightEntity;
    }

    Entity LogicScene::getDirectionalLight() const
    {
        // Find the entity with DirectionalLightComponent
        auto view = m_Registry.view<DirectionalLightComponent>();
        for (auto entity : view)
        {
            return {entity, const_cast<LogicScene*>(this)};
        }

        return {};
    }

    Entity LogicScene::createMeshEntity(const std::string& name, const std::string& meshPath)
    {
        Entity meshEntity = createEntity(name);
        meshEntity.addComponent<TransformComponent>();
        meshEntity.addComponent<RawMeshComponent>(meshPath);
        return meshEntity;
    }

    std::vector<gfx::Renderable> LogicScene::cookRenderables()
    {
        std::vector<gfx::Renderable> renderables;

        auto view = m_Registry.view<RawMeshComponent, TransformComponent>();
        for (auto entity : view)
        {
            const auto& meshComponent      = view.get<RawMeshComponent>(entity);
            const auto& transformComponent = view.get<TransformComponent>(entity);

            if (meshComponent.mesh)
            {
                gfx::Renderable renderable;
                renderable.mesh        = meshComponent.mesh;
                renderable.modelMatrix = transformComponent.getTransform();
                renderables.push_back(renderable);
            }
        }

        return renderables;
    }

    void LogicScene::onLoad()
    {
        // TODO
        m_IsLoaded = true;
    }

    void LogicScene::onTick(float /*deltaTime*/)
    {
        // TODO
        if (m_SimulationStatus != LogicSceneSimulationStatus::eSimulating)
        {
            return;
        }
    }

    void LogicScene::onFixedTick()
    {
        // TODO
    }

    void LogicScene::onUnload()
    {
        // TODO
    }

    void LogicScene::saveTo(const std::filesystem::path& dstPath)
    {
        m_Path = dstPath;
        io::serialize(this, dstPath);
    }

    void LogicScene::clearAndLoad()
    {
        if (std::filesystem::exists(m_Path))
        {
            m_Registry.clear();
            if (io::deserialize(this, m_Path))
            {
                initAfterDeserializing();
            }
        }
    }

    void LogicScene::loadFrom(const std::filesystem::path& srcPath)
    {
        if (std::filesystem::exists(srcPath))
        {
            m_Registry.clear();
            m_Path = srcPath;
            if (io::deserialize(this, m_Path))
            {
                initAfterDeserializing();
            }
        }
    }

    std::vector<Entity> LogicScene::getEntitiesSortedByName()
    {
        std::vector<Entity> entityVector;
        for (auto [uuid, entity] : *m_EntityMap)
        {
            entityVector.emplace_back(entity);
        }

        std::sort(entityVector.begin(), entityVector.end(), [this](const auto& a, const auto& b) {
            const auto& nameA = getNameFromEntity(a);
            const auto& nameB = getNameFromEntity(b);

            int numberA = extractEntityNumber(nameA);
            int numberB = extractEntityNumber(nameB);

            if (numberA != 0 && numberB != 0)
            {
                return numberA < numberB;
            }

            return nameA < nameB;
        });

        return entityVector;
    }

    void LogicScene::createDefaultEntities()
    {
        // TODO
    }

    std::string LogicScene::getNameFromEntity(Entity entity) const
    {
        if (const auto* nameComponent = m_Registry.try_get<NameComponent>(entity))
        {
            return nameComponent->name;
        }
        else
        {
            return "Untitled Entity";
        }
    }

    int LogicScene::extractEntityNumber(const std::string& name)
    {
        std::smatch match;
        if (std::regex_search(name, match, std::regex {R"((\d+))"}))
        {
            return std::stoi(match[1]);
        }
        return 0;
    }

    std::string LogicScene::extractEntityName(const std::string& name)
    {
        std::smatch match;

        if (std::regex_search(name, match, std::regex {R"((.*?)\s*\((\d+)\))"}))
        {
            return match[1].str();
        }

        return "";
    }

    void LogicScene::initAfterDeserializing()
    {
        m_EntityMap->clear();
        m_Name2CountMap.clear();

        m_Registry.view<IDComponent, NameComponent>().each(
            [this](entt::entity entity, IDComponent& id, NameComponent& name) {
                (*m_EntityMap)[id.id] = {entity, this};
                auto entityName       = extractEntityName(name.name);
                if (m_Name2CountMap.count(entityName) == 0)
                    m_Name2CountMap[entityName] = 0;
                m_Name2CountMap[entityName]++;
            });
    }

    template<typename T>
    void LogicScene::onComponentAdded(Entity entity, T& component)
    {}

    ON_COMPONENT_ADDED(IDComponent) {}
    ON_COMPONENT_ADDED(NameComponent) {}
    ON_COMPONENT_ADDED(TransformComponent) {}
    ON_COMPONENT_ADDED(CameraComponent) {}
    ON_COMPONENT_ADDED(DirectionalLightComponent) {}
    ON_COMPONENT_ADDED(RawMeshComponent)
    {
        (void)entity; // Unused, avoid warning
        if (!component.meshPath.empty())
        {
            component.mesh = resource::loadResource<gfx::MeshManager>(component.meshPath);
        }
    }
} // namespace vultra