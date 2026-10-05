#include <vultra/scene/render_nodes.hpp>
#include <vultra/scene/scene_render_state.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <cmath>
#include <stdexcept>

namespace vultra
{
    namespace
    {
        void validateAffine(const Node& node, const glm::mat4& transform)
        {
            for (int column = 0; column < 4; ++column)
            {
                for (int row = 0; row < 4; ++row)
                {
                    if (!std::isfinite(transform[column][row]))
                    {
                        throw std::invalid_argument(node.name() + ": camera/light transform is non-finite");
                    }
                }
            }
            if (transform[0][3] != 0 || transform[1][3] != 0 || transform[2][3] != 0 || transform[3][3] != 1)
            {
                throw std::invalid_argument(node.name() + ": camera/light transform must be affine");
            }
        }

        glm::vec3 normalizedAxis(const Node& node, glm::vec3 axis)
        {
            const auto length = glm::length(axis);
            if (!std::isfinite(length) || length == 0)
            {
                throw std::invalid_argument(node.name() + ": camera/light axis is singular");
            }
            return axis / length;
        }
    } // namespace

    bool SceneRenderState::update(const SceneTree& tree, Extent size)
    {
        if (size.empty())
        {
            throw std::invalid_argument("Scene camera requires a positive render extent");
        }
        const auto& changes      = tree.changes();
        const bool  frameChanged = m_Root != tree.root().id() || m_Size != size ||
                                   m_Applied.structure != changes.structure ||
                                   m_Applied.transforms != changes.transforms || m_Applied.camera != changes.camera ||
                                   m_Applied.lighting != changes.lighting;
        const bool  environmentChanged = frameChanged || m_Applied.environment != changes.environment;
        if (!environmentChanged)
        {
            return false;
        }
        environmentIntensity = 1;
        if (const auto* environment = tree.find(tree.currentEnvironment()))
        {
            environmentIntensity = static_cast<const EnvironmentNode&>(*environment).settings().intensity;
        }
        if (!frameChanged)
        {
            m_Applied = changes;
            return true;
        }
        camera.reset();
        lights.clear();
        sceneLighting      = tree.usesSceneLighting();
        const auto current = tree.currentCamera();
        auto       visit   = [&](const auto& self, const Node& node, const glm::mat4& parent) -> void
        {
            const auto transform = parent * node.localTransform();
            if (node.kind() == NodeKind::eCamera && node.id() == current)
            {
                validateAffine(node, transform);
                const auto& settings = static_cast<const CameraNode&>(node).settings();
                const auto  eye      = glm::vec3(transform[3]);
                const auto  forward  = normalizedAxis(node, -glm::vec3(transform[2]));
                const auto  right    = normalizedAxis(node, glm::cross(forward, glm::vec3(transform[1])));
                // Ignore inherited scale: the camera's axes remain an orthonormal right-handed basis.
                const auto up = glm::cross(right, forward);
                camera        = RenderCamera {glm::lookAtRH(eye, eye + forward, up),
                                              glm::perspectiveRH_ZO(settings.verticalFov,
                                                                    float(size.width) / size.height,
                                                                    settings.nearPlane,
                                                                    settings.farPlane),
                                              settings.nearPlane,
                                              settings.farPlane};
            }
            if (node.kind() == NodeKind::eLight)
            {
                validateAffine(node, transform);
                const auto& light     = static_cast<const LightNode&>(node);
                const auto& settings  = light.settings();
                const auto  direction = light.lightKind() == RenderLightKind::ePoint ?
                                            glm::vec3(0, 0, 1) :
                                            normalizedAxis(node, glm::vec3(transform[2]));
                if (lights.size() == kMaxRenderLights)
                {
                    throw std::invalid_argument("Scene exceeds the renderer's 64-light limit");
                }
                lights.push_back({light.lightKind(),
                                  glm::vec3(transform[3]),
                                  direction,
                                  {settings.red, settings.green, settings.blue},
                                  settings.intensity,
                                  settings.range,
                                  settings.innerCone,
                                  settings.outerCone});
            }
            for (const auto& child : node.children())
            {
                self(self, *child, transform);
            }
        };
        visit(visit, tree.root(), glm::mat4(1));
        m_Root    = tree.root().id();
        m_Size    = size;
        m_Applied = changes;
        return true;
    }

    std::filesystem::path sceneEnvironmentPath(const SceneTree&             tree,
                                               const ProjectManifest&       project,
                                               const std::filesystem::path& projectRoot)
    {
        AssetId asset {};
        if (const auto* node = tree.find(tree.currentEnvironment()))
        {
            asset = static_cast<const EnvironmentNode&>(*node).radianceAsset();
        }
        else if (project.environment)
        {
            asset = *project.environment;
        }
        if (!asset.value.valid())
        {
            return {};
        }
        const auto& path = project.asset(asset).path;
        if (path.extension() != ".hdr")
        {
            throw std::invalid_argument("Environment asset must be a Radiance HDR image");
        }
        return projectRoot / path;
    }

    std::optional<std::span<const RenderLight>> SceneRenderState::lighting() const
    {
        if (sceneLighting)
        {
            return std::span<const RenderLight>(lights);
        }
        return std::nullopt;
    }
} // namespace vultra
