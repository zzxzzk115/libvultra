#pragma once

#include "vultra/core/base/uuid.hpp"
#include "vultra/core/math/math.hpp"
#include "vultra/core/rhi/texture.hpp"
#include "vultra/function/openxr/xr_helper.hpp"
#include "vultra/function/renderer/mesh_resource.hpp"

#include <cereal/cereal.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>

namespace vultra
{
#define COMPONENT_NAME(comp) \
    static std::string GetName() { return #comp; }

    // -------- Basic --------

    struct IDComponent
    {
        COMPONENT_NAME(ID)

        CoreUUID id;

        // NOLINTBEGIN
        template<class Archive>
        void serialize(Archive& archive)
        {
            archive(CEREAL_NVP(id));
        }
        // NOLINTEND

        IDComponent() = default;
        explicit IDComponent(CoreUUID aID) : id(aID) {}
        IDComponent(const IDComponent&) = default;

        std::string getIdString() const { return id.toString(); }
        void        setIdByString(std::string aID)
        {
            auto optionalId = CoreUUID::fromString(aID);
            if (!optionalId.isNil())
            {
                id = optionalId;
            }
        }
    };

    struct NameComponent
    {
        COMPONENT_NAME(Name)

        std::string name;

        // NOLINTBEGIN
        template<class Archive>
        void serialize(Archive& archive)
        {
            archive(CEREAL_NVP(name));
        }
        // NOLINTEND

        NameComponent()                     = default;
        NameComponent(const NameComponent&) = default;
        explicit NameComponent(const std::string& aName) : name(aName) {}
    };

    struct TransformComponent
    {
        COMPONENT_NAME(Transform)

        glm::vec3 position {0.0f};
        glm::vec3 scale {1.0f};

        // NOLINTBEGIN
        template<class Archive>
        void serialize(Archive& archive)
        {
            archive(CEREAL_NVP(position), cereal::make_nvp("Rotation", m_Rotation), CEREAL_NVP(scale));
        }
        // NOLINTEND

        TransformComponent()                          = default;
        TransformComponent(const TransformComponent&) = default;
        TransformComponent(const glm::vec3& aPosition, const glm::vec3& aScale) : position(aPosition), scale(aScale) {}

        glm::mat4 getTransform() const { return math::getTransformMatrix(position, m_Rotation, scale); }

        glm::vec3 getRotationEuler() const { return m_RotationEuler; }

        void setRotationEuler(const glm::vec3& euler)
        {
            m_RotationEuler = euler;
            m_Rotation      = glm::quat(glm::radians(euler));
        }

        glm::quat getRotation() const { return m_Rotation; }

        void setRotation(glm::quat rotation)
        {
            m_Rotation      = rotation;
            m_RotationEuler = glm::degrees(glm::eulerAngles(rotation));
        }

        glm::vec3 forward() const { return m_Rotation * glm::vec3(0, 0, -1); }
        glm::vec3 right() const { return m_Rotation * glm::vec3(1, 0, 0); }
        glm::vec3 up() const { return m_Rotation * glm::vec3(0, 1, 0); }

    private:
        glm::vec3 m_RotationEuler {0, 0, 0};
        glm::quat m_Rotation {1, 0, 0, 0};
    };

    struct SceneGraphComponent
    {
        COMPONENT_NAME(SceneGraph)

        CoreUUID              parentUUID;
        std::vector<CoreUUID> childrenUUIDs;

        // NOLINTBEGIN
        template<class Archive>
        void serialize(Archive& archive)
        {
            archive(CEREAL_NVP(parentUUID), CEREAL_NVP(childrenUUIDs));
        }
        // NOLINTEND

        SceneGraphComponent()                           = default;
        SceneGraphComponent(const SceneGraphComponent&) = default;
    };

    enum class EntityFlags : uint32_t
    {
        eNone        = 0,
        eStatic      = 1 << 0,
        eDontDestroy = 1 << 1,
        eVisible     = 1 << 2,

        eDefault = eVisible
    };

    struct EntityFlagsComponent
    {
        COMPONENT_NAME(EntityFlags)

        uint32_t flags {static_cast<uint32_t>(EntityFlags::eDefault)};

        // NOLINTBEGIN
        template<class Archive>
        void serialize(Archive& archive)
        {
            archive(CEREAL_NVP(flags));
        }
        // NOLINTEND

        EntityFlagsComponent()                            = default;
        EntityFlagsComponent(const EntityFlagsComponent&) = default;
        explicit EntityFlagsComponent(uint32_t aFlags) : flags(aFlags) {}
    };

    // -------- Rendering --------

    enum class CameraClearFlags
    {
        eColor = 0,
        eSkybox,
    };

    enum class CameraProjection
    {
        ePerspective = 0,
        eOrthographic,
    };

    struct CameraComponent
    {
        COMPONENT_NAME(Camera)

        CameraClearFlags clearFlags {CameraClearFlags::eColor};
        CameraProjection projection {CameraProjection::ePerspective};
        glm::vec4        clearColor {0.192157f, 0.301961f, 0.47451f, 1.0f};
        uint32_t         viewPortWidth {1024};
        uint32_t         viewPortHeight {768};
        float            fov {45.0f};
        float            zNear {0.1f};
        float            zFar {1000.0f};
        bool             isPrimary {true};

        std::string       environmentMapPath;       // Optional environment map path for skybox (IBL) rendering
        Ref<rhi::Texture> environmentMap {nullptr}; // Runtime cache, not serializable

        // NOLINTBEGIN
        template<class Archive>
        void serialize(Archive& archive)
        {
            archive(CEREAL_NVP(clearFlags),
                    CEREAL_NVP(projection),
                    CEREAL_NVP(clearColor),
                    CEREAL_NVP(viewPortWidth),
                    CEREAL_NVP(viewPortHeight),
                    CEREAL_NVP(fov),
                    CEREAL_NVP(zNear),
                    CEREAL_NVP(zFar),
                    CEREAL_NVP(isPrimary),
                    CEREAL_NVP(environmentMapPath));
        }
        // NOLINTEND

        CameraComponent()                       = default;
        CameraComponent(const CameraComponent&) = default;

        explicit CameraComponent(const std::string& envMapPath) : environmentMapPath(envMapPath) {}
    };

    struct XrCameraComponent
    {
        COMPONENT_NAME(XrCamera)

        glm::vec3     position {0.0f};
        glm::quat     rotation {1, 0, 0, 0};
        rhi::Extent2D resolution {1024, 1024};
        glm::mat4     viewMatrix {1.0f};
        float         zNear {0.1f};
        float         zFar {1000.0f};

        float fovAngleLeft {-45.0f};
        float fovAngleRight {45.0f};
        float fovAngleUp {45.0f};
        float fovAngleDown {-45.0f};

        bool isLeftEye {true}; // true: left eye, false: right eye

        // NOLINTBEGIN
        template<class Archive>
        void serialize(Archive& archive)
        {
            archive(CEREAL_NVP(position),
                    CEREAL_NVP(rotation),
                    CEREAL_NVP(resolution),
                    CEREAL_NVP(viewMatrix),
                    CEREAL_NVP(zNear),
                    CEREAL_NVP(zFar),
                    CEREAL_NVP(fovAngleLeft),
                    CEREAL_NVP(fovAngleRight),
                    CEREAL_NVP(fovAngleUp),
                    CEREAL_NVP(fovAngleDown),
                    CEREAL_NVP(isLeftEye));
        }
        // NOLINTEND

        XrCameraComponent()                         = default;
        XrCameraComponent(const XrCameraComponent&) = default;

        explicit XrCameraComponent(bool aIsLeftEye) : isLeftEye(aIsLeftEye) {}
    };

    struct DirectionalLightComponent
    {
        COMPONENT_NAME(DirectionalLight)

        glm::vec3 direction {glm::normalize(glm::vec3(-0.6, -1, -1.2))};
        glm::vec3 color {1.0f, 0.996f, 0.885f};
        float     intensity {1.0f};

        // NOLINTBEGIN
        template<class Archive>
        void serialize(Archive& archive)
        {
            archive(CEREAL_NVP(direction), CEREAL_NVP(color), CEREAL_NVP(intensity));
        }
        // NOLINTEND

        DirectionalLightComponent()                                 = default;
        DirectionalLightComponent(const DirectionalLightComponent&) = default;
        DirectionalLightComponent(const glm::vec3& aDirection, const glm::vec3& aColor, float aIntensity) :
            direction(aDirection), color(aColor), intensity(aIntensity)
        {}
    };

    struct PointLightComponent
    {
        COMPONENT_NAME(PointLight)

        glm::vec3 color {1.0f, 0.996f, 0.885f};
        float     intensity {1.0f};
        float     radius {1.0f};

        // NOLINTBEGIN
        template<class Archive>
        void serialize(Archive& archive)
        {
            archive(CEREAL_NVP(color), CEREAL_NVP(intensity), CEREAL_NVP(radius));
        }
        // NOLINTEND

        PointLightComponent()                           = default;
        PointLightComponent(const PointLightComponent&) = default;
        PointLightComponent(const glm::vec3& aColor, float aIntensity, float aRadius) :
            color(aColor), intensity(aIntensity), radius(aRadius)
        {}
    };

    struct AreaLightComponent
    {
        COMPONENT_NAME(AreaLight)

        float width {2.0f};  // full width (X axis in local light space)
        float height {2.0f}; // full height (Y axis in local light space)

        glm::vec3 color {1.0f, 0.996f, 0.885f};
        float     intensity {1.0f};
        bool      twoSided {false};

        // For raytracing
        Ref<rhi::VertexBuffer> vertexBuffer {nullptr}; // Runtime cache, not serializable
        Ref<rhi::IndexBuffer>  indexBuffer {nullptr};  // Runtime cache, not serializable

        // NOLINTBEGIN
        template<class Archive>
        void serialize(Archive& archive)
        {
            archive(
                CEREAL_NVP(width), CEREAL_NVP(height), CEREAL_NVP(color), CEREAL_NVP(intensity), CEREAL_NVP(twoSided));
        }
        // NOLINTEND

        AreaLightComponent()                          = default;
        AreaLightComponent(const AreaLightComponent&) = default;
        AreaLightComponent(float aWidth, float aHeight, const glm::vec3& aColor, float aIntensity, bool aTwoSided) :
            width(aWidth), height(aHeight), color(aColor), intensity(aIntensity), twoSided(aTwoSided)
        {}
    };

    struct RawMeshComponent
    {
        COMPONENT_NAME(RawMesh)

        std::string meshPath;

        // Runtime cache, not serializable
        Ref<gfx::DefaultMesh> mesh {nullptr};

        // NOLINTBEGIN
        template<class Archive>
        void serialize(Archive& archive)
        {
            archive(CEREAL_NVP(meshPath));
        }
        // NOLINTEND

        RawMeshComponent()                        = default;
        RawMeshComponent(const RawMeshComponent&) = default;
        explicit RawMeshComponent(const std::string& aMeshPath) : meshPath(aMeshPath) {}
    };

    struct MeshComponent
    {
        COMPONENT_NAME(Mesh)

        std::string uuidStr; // vasset UUID string

        // NOLINTBEGIN
        template<class Archive>
        void serialize(Archive& archive)
        {
            archive(CEREAL_NVP(uuidStr));
        }
        // NOLINTEND

        // Runtime only, not serializable
        Ref<gfx::MeshResource> meshResource {nullptr};

        MeshComponent()                     = default;
        MeshComponent(const MeshComponent&) = default;
        explicit MeshComponent(const std::string& aUUIDStr) : uuidStr(aUUIDStr) {}
    };

    template<typename... Component>
    struct ComponentGroup
    {};

#define COMMON_COMPONENT_TYPES
#define ALL_SERIALIZABLE_COMPONENT_TYPES \
    IDComponent, NameComponent, TransformComponent, SceneGraphComponent, EntityFlagsComponent, CameraComponent, \
        DirectionalLightComponent, RawMeshComponent, MeshComponent
#define ALL_COPYABLE_COMPONENT_TYPES COMMON_COMPONENT_TYPES

    using AllCopyableComponents = ComponentGroup<ALL_COPYABLE_COMPONENT_TYPES>;
} // namespace vultra