#include "scene_inspector.hpp"

#include <vultra/api/scene_properties.generated.hpp>
#include <vultra/scene/scene_api.hpp>
#include <vultra/ui/editor_gui.hpp>
#include <vultra/ui/editor_gui_shader_material.hpp>

// GLM requires this opt-in for its matrix decomposition utility.
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <stdexcept>
#include <utility>

namespace vultra
{
    namespace
    {
        struct ObjectScope
        {
            ObjectScope(EditorGuiFrame& frame, const char* id) :
                ui(frame)
            {
                ui.pushId(id);
            }

            ~ObjectScope()
            {
                ui.popId();
            }

            ObjectScope(const ObjectScope&)               = delete;
            ObjectScope&    operator=(const ObjectScope&) = delete;
            EditorGuiFrame& ui;
        };

        bool decomposeTransform(const glm::mat4& transform, glm::vec3& scale, glm::quat& rotation, glm::vec3& position)
        {
            if (transform[0][3] != 0 || transform[1][3] != 0 || transform[2][3] != 0 || transform[3][3] != 1)
            {
                return false;
            }
            auto      normalized = transform;
            glm::vec3 lengths;
            for (int column = 0; column < 3; ++column)
            {
                lengths[column] = glm::length(glm::vec3(transform[column]));
                if (!std::isfinite(lengths[column]) || lengths[column] == 0)
                {
                    return false;
                }
                normalized[column] /= lengths[column];
            }
            // Normalize magnitudes so GLM's absolute determinant threshold does not reject small valid scales.
            glm::vec3 skew;
            glm::vec4 perspective;
            if (!glm::decompose(normalized, scale, rotation, position, skew, perspective))
            {
                return false;
            }
            scale *= lengths;
            return true;
        }

        const char* nodeType(const Node& node)
        {
            switch (node.kind())
            {
                case NodeKind::eGroup:
                    return "Node";
                case NodeKind::eMeshInstance:
                    return "MeshInstance";
                case NodeKind::eCamera:
                    return "Camera";
                case NodeKind::eLight:
                    switch (static_cast<const LightNode&>(node).lightKind())
                    {
                        case RenderLightKind::eDirectional:
                            return "Directional light";
                        case RenderLightKind::ePoint:
                            return "Point light";
                        case RenderLightKind::eSpot:
                            return "Spot light";
                    }
                    break;
                case NodeKind::eEnvironment:
                    return "Environment";
            }
            throw std::logic_error("Unknown inspector node type");
        }
    } // namespace

    SceneInspector::SceneInspector(EditorGui& gui) :
        m_Gui(gui)
    {
    }

    void SceneInspector::drawTree(EditorGuiFrame& ui, const SceneTree& tree)
    {
        if (!tree.find(m_Selected) && !tree.findMaterial(m_Selected))
        {
            m_Selected = tree.root().id();
        }
        auto visit = [&](const auto& self, const Node& node) -> void
        {
            const auto id    = std::format("node-{}", node.id().value);
            auto       flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (node.id() == m_Selected)
            {
                flags |= ImGuiTreeNodeFlags_Selected;
            }
            if (node.children().empty())
            {
                flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            }
            if (!node.parent())
            {
                flags |= ImGuiTreeNodeFlags_DefaultOpen;
            }
            const bool open = ui.treeNodeEx(id.c_str(), flags, "%s", node.name().c_str());
            if (ui.isItemClicked())
            {
                m_Selected = node.id();
            }
            if (open && !node.children().empty())
            {
                for (const auto& child : node.children())
                {
                    self(self, *child);
                }
                ui.treePop();
            }
        };
        visit(visit, tree.root());
        if (!tree.materials().empty())
        {
            ui.separatorText("Resources");
            for (const auto& material : tree.materials())
            {
                const auto id = std::format("resource-{}", material->id().value);
                ui.treeNodeEx(id.c_str(),
                              ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                  ImGuiTreeNodeFlags_SpanAvailWidth |
                                  (material->id() == m_Selected ? ImGuiTreeNodeFlags_Selected : 0),
                              "%s",
                              material->name().c_str());
                if (ui.isItemClicked())
                {
                    m_Selected = material->id();
                }
            }
        }
    }

    void SceneInspector::drawTransform(EditorGuiFrame& ui, Node& node)
    {
        const auto defaults = node.kind() == NodeKind::eGroup || node.kind() == NodeKind::eMeshInstance ?
                                  ImGuiTreeNodeFlags_DefaultOpen :
                                  0;
        if (!ui.treeNodeEx("local-transform", ImGuiTreeNodeFlags_CollapsingHeader | defaults, "Transform (local)"))
        {
            return;
        }
        auto      transform = node.localTransform();
        glm::vec3 scale;
        glm::quat rotation;
        glm::vec3 position;
        if (!decomposeTransform(transform, scale, rotation, position))
        {
            ui.textWrapped("TRS editing requires a nonsingular affine matrix.");
            return;
        }
        auto       angles        = glm::degrees(glm::eulerAngles(rotation));
        bool       moved         = false;
        bool       rotated       = false;
        bool       scaled        = false;
        const auto previousScale = scale;
        {
            EditorGuiInspector inspector(m_Gui, "transform-properties");
            if (inspector)
            {
                moved   = inspector.float3Field({"Node.position", "Position"}, glm::value_ptr(position));
                rotated = inspector.float3Field({"Node.rotation", "Rotation (deg)"}, glm::value_ptr(angles), 0.5f);
                scaled  = inspector.float3Field({"Node.scale", "Scale"}, glm::value_ptr(scale));
            }
        }
        if (!moved && !rotated && !scaled)
        {
            return;
        }
        if (scaled && (std::abs(scale.x) < 0.00001f || std::abs(scale.y) < 0.00001f || std::abs(scale.z) < 0.00001f))
        {
            throw std::invalid_argument("Inspector scale magnitude must be at least 1e-5 on each axis");
        }
        if (moved)
        {
            transform[3] = glm::vec4(position, 1);
        }
        // Apply deltas to the existing basis, preserving shear and signed scale rather than rebuilding a TRS matrix.
        if (rotated)
        {
            const auto delta = glm::mat3_cast(glm::quat(glm::radians(angles)) * glm::inverse(rotation));
            for (int column = 0; column < 3; ++column)
            {
                transform[column] = glm::vec4(delta * glm::vec3(transform[column]), 0);
            }
        }
        if (scaled)
        {
            for (int column = 0; column < 3; ++column)
            {
                transform[column] *= scale[column] / previousScale[column];
            }
        }
        node.setLocalTransform(transform);
    }

    void SceneInspector::drawMesh(EditorGuiFrame& ui, ResearchWorkspace& workspace, MeshInstanceNode& mesh)
    {
        ui.textDisabled("Model: %s", workspace.project().asset(mesh.model()).path.filename().string().c_str());
        const auto count = workspace.materialSlotCount(mesh.id());
        for (uint32_t slot = 0; slot < count; ++slot)
        {
            const auto found = std::ranges::find(mesh.materialOverrides(), slot, &MeshMaterialOverride::slot);
            auto*      material =
                found == mesh.materialOverrides().end() ? nullptr : workspace.scene().findMaterial(found->material);
            const auto label = std::format("Material {}", slot);
            if (ui.beginCombo(label.c_str(), material ? material->name().c_str() : "Imported values"))
            {
                if (ui.selectable("Imported values", !material))
                {
                    mesh.setMaterial(slot, {});
                }
                for (const auto& entry : workspace.scene().materials())
                {
                    ui.pushId(entry->assetId().value.toString().c_str());
                    if (ui.selectable(entry->name().c_str(), entry.get() == material))
                    {
                        mesh.setMaterial(slot, entry->assetId());
                    }
                    ui.popId();
                }
                ui.endCombo();
            }
        }
    }

    void SceneInspector::drawEnvironment(EditorGuiFrame& ui, ResearchWorkspace& workspace, EnvironmentNode& environment)
    {
        bool current = workspace.scene().currentEnvironment() == environment.id();
        {
            EditorGuiInspector inspector(m_Gui, "environment-selection");
            if (inspector && inspector.boolField({"Environment.current", "Current environment"}, &current))
            {
                const auto root   = workspace.scene().root().id();
                m_EnvironmentEdit = current ? EnvironmentEdit {root, environment.id(), environment.radianceAsset()} :
                                              EnvironmentEdit {root};
            }
        }
        const auto asset = environment.radianceAsset();
        const auto preview =
            asset.value.valid() ? workspace.project().asset(asset).path.filename().string() : "Procedural studio";
        if (ui.beginCombo("Radiance", preview.c_str()))
        {
            const auto choose = [&](AssetId radiance)
            {
                if (workspace.scene().currentEnvironment() == environment.id())
                {
                    m_EnvironmentEdit = EnvironmentEdit {workspace.scene().root().id(), environment.id(), radiance};
                }
                else
                {
                    sceneSetEnvironmentAsset(workspace.scene(),
                                             workspace.project(),
                                             environment.id(),
                                             radiance.value.valid() ? radiance.value.toString() : "");
                }
            };
            if (ui.selectable("Procedural studio", !asset.value.valid()))
            {
                choose({});
            }
            for (const auto& entry : workspace.project().assets())
            {
                if (entry.path.extension() == ".hdr" &&
                    ui.selectable(entry.path.generic_string().c_str(), entry.id == asset))
                {
                    choose(entry.id);
                }
            }
            ui.endCombo();
        }
        auto               settings = environment.settings();
        EditorGuiInspector inspector(m_Gui, "environment-properties");
        if (inspector && drawEnvironmentSettings(inspector, settings))
        {
            environment.setSettings(settings);
        }
    }

    void SceneInspector::drawProperties(EditorGuiFrame& ui, ResearchWorkspace& workspace)
    {
        auto&      tree = workspace.scene();
        const auto id   = std::format("object-{}", m_Selected.value);
        // Scope ImGui edit state to the object, so switching selection cannot continue editing another object.
        ObjectScope objectScope(ui, id.c_str());
        if (auto* material = tree.findMaterial(m_Selected))
        {
            ui.text("%s | Material", material->name().c_str());
            if (material->kind() == MaterialResource::Kind::eShader)
            {
                auto instance = material->shaderMaterial();
                if (drawShaderMaterialInspector(m_Gui,
                                                workspace.shaderAsset(material->id()),
                                                instance,
                                                &workspace.project()))
                {
                    material->setShaderMaterial(std::move(instance));
                }
                return;
            }
            ui.textWrapped("Shared numeric parameters. Imported textures remain bound.");
            auto               parameters = material->parameters();
            EditorGuiInspector inspector(m_Gui, id.c_str());
            if (inspector && drawMaterialParameters(inspector, parameters))
            {
                material->setParameters(parameters);
            }
            return;
        }
        auto* node = tree.find(m_Selected);
        if (!node)
        {
            ui.textDisabled("Select a node or resource");
            return;
        }
        ui.textDisabled("%s", nodeType(*node));
        auto name = node->name();
        {
            EditorGuiInspector inspector(m_Gui, id.c_str());
            if (inspector && inspector.textField({"Node.name", "Name"}, &name))
            {
                node->setName(std::move(name));
            }
        }
        if (node->kind() != NodeKind::eEnvironment)
        {
            drawTransform(ui, *node);
        }
        switch (node->kind())
        {
            case NodeKind::eGroup:
                break;
            case NodeKind::eMeshInstance:
                drawMesh(ui, workspace, static_cast<MeshInstanceNode&>(*node));
                break;
            case NodeKind::eCamera: {
                bool current = tree.currentCamera() == node->id();
                {
                    EditorGuiInspector inspector(m_Gui, "camera-selection");
                    if (inspector && inspector.boolField({"Camera.current", "Current camera"}, &current))
                    {
                        tree.setCurrentCamera(current ? node->id() : ObjectId {});
                    }
                }
                auto&              camera   = static_cast<CameraNode&>(*node);
                auto               settings = camera.settings();
                EditorGuiInspector inspector(m_Gui, "camera-properties");
                if (inspector && drawCameraSettings(inspector, settings))
                {
                    camera.setSettings(settings);
                }
                break;
            }
            case NodeKind::eLight: {
                auto&              light    = static_cast<LightNode&>(*node);
                auto               settings = light.settings();
                EditorGuiInspector inspector(m_Gui, "light-properties");
                if (inspector && drawLightSettings(inspector, settings))
                {
                    light.setSettings(settings);
                }
                break;
            }
            case NodeKind::eEnvironment:
                drawEnvironment(ui, workspace, static_cast<EnvironmentNode&>(*node));
                break;
        }
    }

    void SceneInspector::applyPending(ResearchWorkspace& workspace)
    {
        if (auto edit = std::exchange(m_EnvironmentEdit, std::nullopt))
        {
            // A scene replacement cancels UI operations queued against the old object IDs.
            if (edit->root == workspace.scene().root().id())
            {
                workspace.setEnvironment(edit->node, edit->radiance);
            }
        }
    }
} // namespace vultra
