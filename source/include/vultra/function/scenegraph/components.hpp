#pragma once

#include "vultra/core/base/uuid.hpp"
#include "vultra/core/math/math.hpp"
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

        std::string getIdString() const { return to_string(id); }
        void        setIdByString(std::string aID)
        {
            auto optionalId = CoreUUID::from_string(aID);
            if (optionalId.has_value())
            {
                id = optionalId.value();
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
            m_RotationEuler = glm::eulerAngles(rotation);
        }

        glm::vec3 forward() const { return m_Rotation * glm::vec3(0, 0, -1); }

    private:
        glm::vec3 m_RotationEuler {0, 0, 0};
        glm::quat m_Rotation {1, 0, 0, 0};
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
        float     intensity {5.0f};

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

    struct RawMeshComponent
    {
        COMPONENT_NAME(RawMesh)

        std::string meshPath;

        // Runtime cache, not serializable
        Ref<gfx::MeshResource> mesh {nullptr};

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

    template<typename... Component>
    struct ComponentGroup
    {};

#define COMMON_COMPONENT_TYPES
#define ALL_SERIALIZABLE_COMPONENT_TYPES \
    IDComponent, NameComponent, TransformComponent, CameraComponent, DirectionalLightComponent, RawMeshComponent
#define ALL_COPYABLE_COMPONENT_TYPES COMMON_COMPONENT_TYPES

    using AllCopyableComponents = ComponentGroup<ALL_COPYABLE_COMPONENT_TYPES>;
} // namespace vultra