#include "editor_app/ui/windows/inspector_window.hpp"

#include "common/file_dialog.hpp"
#include "common/ui_widgets.hpp"
#include "editor_app/editor_history.hpp"
#include "editor_app/selection.hpp"

#include <IconsMaterialDesignIcons.h>
#include <ImGuiFileDialog/ImGuiFileDialog.h>
#include <vultra/function/rendering/render_structs.hpp>
#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/camera_service.hpp>
#include <vultra/function/services/imgui_service.hpp>
#include <vultra/function/services/render_backend_service.hpp>
#include <vultra/function/services/render_service.hpp>
#include <vultra/function/services/scene_service.hpp>
#include <vultra/function/services/world_service.hpp>
#include <vultra/function/world/components/animator_component.hpp>
#include <vultra/function/world/components/box_shape_component.hpp>
#include <vultra/function/world/components/camera_component.hpp>
#include <vultra/function/world/components/capsule_shape_component.hpp>
#include <vultra/function/world/components/entity_status_component.hpp>
#include <vultra/function/world/components/environment_component.hpp>
#include <vultra/function/world/components/gaussian_splat_component.hpp>
#include <vultra/function/world/components/hierarchy_component.hpp>
#include <vultra/function/world/components/id_component.hpp>
#include <vultra/function/world/components/light_component.hpp>
#include <vultra/function/world/components/mesh_component.hpp>
#include <vultra/function/world/components/name_component.hpp>
#include <vultra/function/world/components/prefab_instance_component.hpp>
#include <vultra/function/world/components/reflection_probe_component.hpp>
#include <vultra/function/world/components/rigid_body_component.hpp>
#include <vultra/function/world/components/script_component.hpp>
#include <vultra/function/world/components/sphere_shape_component.hpp>
#include <vultra/function/world/components/transform_component.hpp>
#include <vultra/function/world/components/xr_view_component.hpp>
#include <vultra/function/world/world.hpp>

#include <vasset/vanimation.hpp>

#include <entt/meta/meta.hpp>
#include <glm/common.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>
#include <imgui.h>
#include <sol/sol.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace vultra_app
{
    namespace
    {
        constexpr uint64_t kModelPreviewTargetReleaseDelayFrames = 3;

        entt::entity findEntityByUUID(vultra::World& world, const vultra::CoreUUID& uuid)
        {
            auto& reg  = world.registry();
            auto  view = reg.view<vultra::IDComponent>();
            for (auto e : view)
            {
                if (view.get<vultra::IDComponent>(e).uuid == uuid)
                    return e;
            }
            return entt::null;
        }

        constexpr const char* kAssetUuidPayload = "VULTRA_ASSET_UUID";
        constexpr const char* kScriptDialogKey  = "InspectorSelectLuaScript";

        template<std::size_t N>
        void copyName(std::array<char, N>& dst, const std::string& src)
        {
            const auto count = std::min(dst.size() - 1, src.size());
            std::memcpy(dst.data(), src.data(), count);
            dst[count] = '\0';
        }

        bool sourceAssetHasExtension(const std::filesystem::path& path, std::initializer_list<const char*> exts)
        {
            auto ext = path.extension().generic_string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return std::any_of(exts.begin(), exts.end(), [&](const char* candidate) { return ext == candidate; });
        }

        bool isModelSourceAsset(const std::filesystem::path& path)
        {
            return sourceAssetHasExtension(path, {".gltf", ".glb", ".obj", ".fbx", ".dae"});
        }

        bool isEditableSourceText(const std::filesystem::path& path)
        {
            auto name = path.filename().generic_string();
            auto ext  = path.extension().generic_string();
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });

            const auto endsWith = [&](const char* suffix) {
                const std::string_view text(name);
                const std::string_view tail(suffix);
                return text.size() >= tail.size() && text.substr(text.size() - tail.size()) == tail;
            };

            return ext == ".lua" || ext == ".vshader" || ext == ".glsl" || ext == ".vert" || ext == ".frag" ||
                   ext == ".comp" || ext == ".json" || ext == ".vproject" || ext == ".vscn" || ext == ".txt" ||
                   ext == ".md" || endsWith(".vfeature.lua") || endsWith(".vsrp.lua") || endsWith(".vshaderlib.lua") ||
                   endsWith(".vso.lua");
        }

        void drawImagePreviewPlaceholder(const std::filesystem::path& path, const char* note)
        {
            ImGui::TextUnformatted("Preview");
            const float  size = std::min(ImGui::GetContentRegionAvail().x, 220.0f);
            const ImVec2 pos  = ImGui::GetCursorScreenPos();
            ImDrawList*  dl   = ImGui::GetWindowDrawList();
            const ImVec2 max {pos.x + size, pos.y + size};

            const float tile = 16.0f;
            for (float y = pos.y; y < max.y; y += tile)
            {
                for (float x = pos.x; x < max.x; x += tile)
                {
                    const bool dark =
                        (static_cast<int>((x - pos.x) / tile) + static_cast<int>((y - pos.y) / tile)) % 2 == 0;
                    dl->AddRectFilled(ImVec2(x, y),
                                      ImVec2(std::min(x + tile, max.x), std::min(y + tile, max.y)),
                                      dark ? IM_COL32(62, 66, 72, 255) : IM_COL32(82, 88, 96, 255));
                }
            }
            dl->AddRect(pos, max, IM_COL32(150, 160, 175, 255), 6.0f, 0, 1.5f);
            const auto label = path.filename().generic_string();
            const auto text  = ImGui::CalcTextSize(label.c_str());
            dl->AddText(ImVec2(pos.x + (size - text.x) * 0.5f, pos.y + (size - text.y) * 0.5f),
                        IM_COL32(235, 238, 242, 255),
                        label.c_str());
            ImGui::Dummy(ImVec2(size, size));
            ImGui::TextDisabled("%s", note);
        }

        std::string formatFileSize(uintmax_t bytes)
        {
            constexpr double kib = 1024.0;
            constexpr double mib = kib * 1024.0;
            constexpr double gib = mib * 1024.0;

            char buffer[64] {};
            if (bytes >= static_cast<uintmax_t>(gib))
                std::snprintf(buffer, sizeof(buffer), "%.2f GiB", static_cast<double>(bytes) / gib);
            else if (bytes >= static_cast<uintmax_t>(mib))
                std::snprintf(buffer, sizeof(buffer), "%.2f MiB", static_cast<double>(bytes) / mib);
            else if (bytes >= static_cast<uintmax_t>(kib))
                std::snprintf(buffer, sizeof(buffer), "%.1f KiB", static_cast<double>(bytes) / kib);
            else
                std::snprintf(buffer, sizeof(buffer), "%llu B", static_cast<unsigned long long>(bytes));
            return buffer;
        }

        const char* displayComponentName(const char* metaName)
        {
            if (std::strcmp(metaName, "TransformComponent") == 0)
                return "Transform";
            if (std::strcmp(metaName, "EntityStatusComponent") == 0)
                return "Status";
            if (std::strcmp(metaName, "MeshComponent") == 0)
                return "Mesh";
            if (std::strcmp(metaName, "AnimatorComponent") == 0)
                return "Animator";
            if (std::strcmp(metaName, "GaussianSplatComponent") == 0)
                return "Gaussian Splat";
            if (std::strcmp(metaName, "CameraComponent") == 0)
                return "Camera";
            if (std::strcmp(metaName, "XRViewComponent") == 0)
                return "XR View";
            if (std::strcmp(metaName, "EnvironmentComponent") == 0)
                return "Environment";
            if (std::strcmp(metaName, "ReflectionProbeComponent") == 0)
                return "Reflection Probe";
            if (std::strcmp(metaName, "LightComponent") == 0)
                return "Light";
            if (std::strcmp(metaName, "RigidBodyComponent") == 0)
                return "Rigid Body";
            if (std::strcmp(metaName, "BoxShapeComponent") == 0)
                return "Box Shape";
            if (std::strcmp(metaName, "SphereShapeComponent") == 0)
                return "Sphere Shape";
            if (std::strcmp(metaName, "CapsuleShapeComponent") == 0)
                return "Capsule Shape";
            if (std::strcmp(metaName, "ScriptComponent") == 0)
                return "Script";
            return metaName;
        }

        template<typename Component>
        const char* componentDisplayName()
        {
            return "Component";
        }

        template<>
        const char* componentDisplayName<vultra::TransformComponent>()
        {
            return "Transform";
        }

        template<>
        const char* componentDisplayName<vultra::HierarchyComponent>()
        {
            return "Hierarchy";
        }

        template<>
        const char* componentDisplayName<vultra::MeshComponent>()
        {
            return "Mesh";
        }

        template<>
        const char* componentDisplayName<vultra::AnimatorComponent>()
        {
            return "Animator";
        }

        template<>
        const char* componentDisplayName<vultra::GaussianSplatComponent>()
        {
            return "Gaussian Splat";
        }

        template<>
        const char* componentDisplayName<vultra::CameraComponent>()
        {
            return "Camera";
        }

        template<>
        const char* componentDisplayName<vultra::XRViewComponent>()
        {
            return "XR View";
        }

        template<>
        const char* componentDisplayName<vultra::EnvironmentComponent>()
        {
            return "Environment";
        }

        template<>
        const char* componentDisplayName<vultra::ReflectionProbeComponent>()
        {
            return "Reflection Probe";
        }

        template<>
        const char* componentDisplayName<vultra::LightComponent>()
        {
            return "Light";
        }

        template<>
        const char* componentDisplayName<vultra::RigidBodyComponent>()
        {
            return "Rigid Body";
        }

        template<>
        const char* componentDisplayName<vultra::BoxShapeComponent>()
        {
            return "Box Shape";
        }

        template<>
        const char* componentDisplayName<vultra::SphereShapeComponent>()
        {
            return "Sphere Shape";
        }

        template<>
        const char* componentDisplayName<vultra::CapsuleShapeComponent>()
        {
            return "Capsule Shape";
        }

        template<>
        const char* componentDisplayName<vultra::ScriptComponent>()
        {
            return "Script";
        }

        template<>
        const char* componentDisplayName<vultra::PrefabInstanceComponent>()
        {
            return "Prefab";
        }

        std::string displayFieldName(const char* raw)
        {
            if (raw == nullptr)
                return {};

            std::string out;
            out.reserve(std::strlen(raw) + 4);
            for (const char* c = raw; *c != '\0'; ++c)
            {
                if (c != raw && std::isupper(static_cast<unsigned char>(*c)))
                    out.push_back(' ');
                out.push_back(*c);
            }
            if (!out.empty())
                out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
            return out;
        }

        bool
        drawVec3Control(const char* label, glm::vec3& value, const glm::vec3& resetValue, const float speed = 0.05f)
        {
            bool       changed           = false;
            const auto cleanDisplayValue = [](float& v) {
                if (std::abs(v) < 0.0005f)
                    v = 0.0f;
            };
            cleanDisplayValue(value.x);
            cleanDisplayValue(value.y);
            cleanDisplayValue(value.z);

            ImGui::PushID(label);
            ImGui::Columns(2, nullptr, false);
            ImGui::SetColumnWidth(0, 92.0f);
            ImGui::TextUnformatted(label);
            ImGui::NextColumn();

            const float  lineHeight = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
            const ImVec2 buttonSize {lineHeight + 3.0f, lineHeight};
            const float  itemWidth = std::max(
                42.0f,
                (ImGui::GetContentRegionAvail().x - buttonSize.x * 3.0f - ImGui::GetStyle().ItemSpacing.x * 6.0f) /
                    3.0f);

            auto axis =
                [&](const char* axisLabel, float& axisValue, float reset, ImVec4 color, ImVec4 hovered, ImVec4 active) {
                    ImGui::PushStyleColor(ImGuiCol_Button, color);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hovered);
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
                    if (ImGui::Button(axisLabel, buttonSize))
                    {
                        axisValue = reset;
                        changed   = true;
                    }
                    ImGui::PopStyleColor(3);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(itemWidth);
                    changed |= ImGui::DragFloat(
                        (std::string("##") + axisLabel).c_str(), &axisValue, speed, 0.0f, 0.0f, "%.3f");
                    ImGui::SameLine();
                };

            axis("X",
                 value.x,
                 resetValue.x,
                 {0.55f, 0.16f, 0.18f, 1.0f},
                 {0.75f, 0.22f, 0.24f, 1.0f},
                 {0.9f, 0.28f, 0.32f, 1.0f});
            axis("Y",
                 value.y,
                 resetValue.y,
                 {0.20f, 0.46f, 0.20f, 1.0f},
                 {0.28f, 0.64f, 0.28f, 1.0f},
                 {0.34f, 0.78f, 0.34f, 1.0f});
            axis("Z",
                 value.z,
                 resetValue.z,
                 {0.16f, 0.28f, 0.58f, 1.0f},
                 {0.22f, 0.38f, 0.78f, 1.0f},
                 {0.30f, 0.48f, 0.94f, 1.0f});
            ImGui::NewLine();

            ImGui::Columns(1);
            ImGui::PopID();
            return changed;
        }

        glm::quat rotationFromDirection(const glm::vec3& direction);
        glm::vec3 directionFromTransform(const vultra::TransformComponent& transform);

        bool drawQuaternionDeltaControl(const char* label, glm::quat& rotation, const vultra::CoreUUID& entityId)
        {
            struct RotationEditState
            {
                glm::vec3 degrees {};
                glm::quat source {1.0f, 0.0f, 0.0f, 0.0f};
            };
            static std::unordered_map<vultra::CoreUUID, RotationEditState> s_RotationEditState;

            const auto quaternionChanged = [](const glm::quat& a, const glm::quat& b) {
                return std::abs(glm::dot(glm::normalize(a), glm::normalize(b))) < 0.99999f;
            };

            auto [it, inserted] =
                s_RotationEditState.try_emplace(entityId,
                                                RotationEditState {
                                                    .degrees = glm::degrees(glm::eulerAngles(rotation)),
                                                    .source  = glm::normalize(rotation),
                                                });
            auto& editState = it->second;
            if (!inserted && quaternionChanged(editState.source, rotation))
            {
                editState.degrees = glm::degrees(glm::eulerAngles(rotation));
                editState.source  = glm::normalize(rotation);
            }
            auto&     editDegrees = editState.degrees;
            glm::vec3 nextDegrees = editDegrees;

            if (!drawVec3Control(label, nextDegrees, glm::vec3 {0.0f}, 0.5f))
                return false;

            const glm::vec3 deltaDegrees = nextDegrees - editDegrees;
            editDegrees                  = nextDegrees;

            if (glm::dot(deltaDegrees, deltaDegrees) <= 1e-8f)
            {
                rotation = glm::quat {1.0f, 0.0f, 0.0f, 0.0f};
                return true;
            }

            const glm::vec3 deltaRadians = glm::radians(deltaDegrees);
            const glm::quat qx           = glm::angleAxis(deltaRadians.x, glm::vec3 {1.0f, 0.0f, 0.0f});
            const glm::quat qy           = glm::angleAxis(deltaRadians.y, glm::vec3 {0.0f, 1.0f, 0.0f});
            const glm::quat qz           = glm::angleAxis(deltaRadians.z, glm::vec3 {0.0f, 0.0f, 1.0f});
            rotation                     = glm::normalize(qz * qy * qx * rotation);
            editState.source             = rotation;
            return true;
        }

        bool drawTransformComponentFields(vultra::TransformComponent&   transform,
                                          const vultra::CoreUUID&       entityId,
                                          const vultra::LightComponent* light = nullptr)
        {
            bool changed = false;
            ImGui::Indent();

            changed |= drawVec3Control("Position", transform.position, glm::vec3 {0.0f}, 0.05f);

            if (light && (light->kind == 0 || light->kind == 2))
            {
                glm::vec3 direction = directionFromTransform(transform);
                if (drawVec3Control("Direction", direction, glm::vec3 {0.0f, -1.0f, 0.0f}, 0.01f))
                {
                    transform.rotation = rotationFromDirection(direction);
                    changed            = true;
                }
            }
            else
            {
                if (drawQuaternionDeltaControl("Rotation", transform.rotation, entityId))
                    changed = true;
            }

            changed |= drawVec3Control("Scale", transform.scale, glm::vec3 {1.0f}, 0.05f);

            ImGui::Unindent();

            if (changed)
                transform.dirty = true;
            return changed;
        }

        glm::quat rotationFromDirection(const glm::vec3& direction)
        {
            const float len2 = glm::dot(direction, direction);
            const auto  dir  = len2 > 1e-8f ? direction * glm::inversesqrt(len2) : glm::vec3 {0.0f, -1.0f, 0.0f};
            glm::vec3   up {0.0f, 1.0f, 0.0f};
            if (std::abs(glm::dot(up, dir)) > 0.95f)
                up = glm::vec3 {1.0f, 0.0f, 0.0f};
            return glm::normalize(glm::quatLookAtRH(dir, up));
        }

        glm::vec3 directionFromTransform(const vultra::TransformComponent& transform)
        {
            const auto direction = transform.rotation * glm::vec3 {0.0f, 0.0f, -1.0f};
            const auto len2      = glm::dot(direction, direction);
            return len2 > 1e-8f ? direction * glm::inversesqrt(len2) : glm::vec3 {0.0f, -1.0f, 0.0f};
        }

        bool drawLightComponentFields(vultra::LightComponent& light)
        {
            bool                  changed       = false;
            constexpr const char* kKindLabels[] = {"Directional", "Point", "Spot", "Rectangle Area"};
            int                   kindIndex     = static_cast<int>(std::min(light.kind, 3u));
            if (ImGui::Combo("Kind", &kindIndex, kKindLabels, IM_ARRAYSIZE(kKindLabels)))
            {
                light.kind = static_cast<uint32_t>(std::clamp(kindIndex, 0, IM_ARRAYSIZE(kKindLabels) - 1));
                changed    = true;
            }

            changed |= ImGui::ColorEdit3("Color", &light.color.x);
            changed |= ImGui::DragFloat("Intensity", &light.intensity, 0.05f, 0.0f, 10000.0f, "%.2f");

            if (light.kind == 1 || light.kind == 2)
            {
                changed |= ImGui::DragFloat("Range", &light.range, 0.05f, 0.0f, 1000.0f, "%.2f");
                changed |= ImGui::DragFloat("Radius", &light.radius, 0.01f, 0.0f, 100.0f, "%.3f");
            }
            if (light.kind == 2)
            {
                changed |= ImGui::DragFloat("Inner Cone Degrees", &light.innerConeDegrees, 0.25f, 0.0f, 179.0f, "%.1f");
                changed |= ImGui::DragFloat("Outer Cone Degrees", &light.outerConeDegrees, 0.25f, 0.0f, 179.0f, "%.1f");
                light.outerConeDegrees = std::max(light.outerConeDegrees, light.innerConeDegrees);
            }
            if (light.kind == 3)
            {
                changed |= ImGui::DragFloat("Width", &light.width, 0.05f, 0.0f, 100.0f, "%.2f");
                changed |= ImGui::DragFloat("Height", &light.height, 0.05f, 0.0f, 100.0f, "%.2f");
            }
            changed |= ImGui::Checkbox("Casts Shadow", &light.castsShadow);
            changed |= ImGui::Checkbox("Two Sided", &light.twoSided);
            return changed;
        }

        bool drawXRViewComponentFields(EditorContext& ctx, vultra::XRViewComponent& xrView)
        {
            bool changed = false;

            changed |= ImGui::Checkbox("Enabled", &xrView.enabled);

            int         trackingOrigin    = static_cast<int>(xrView.trackingOrigin);
            const char* trackingOrigins[] = {"Local", "Stage"};
            if (ImGui::Combo("Tracking Origin", &trackingOrigin, trackingOrigins, IM_ARRAYSIZE(trackingOrigins)))
            {
                xrView.trackingOrigin = static_cast<uint32_t>(std::clamp(trackingOrigin, 0, 1));
                changed               = true;
            }

            int         stereoGraphMode    = static_cast<int>(xrView.stereoGraphMode);
            const char* stereoGraphModes[] = {"Single Graph Stereo"};
            if (ImGui::Combo("Stereo Graph", &stereoGraphMode, stereoGraphModes, IM_ARRAYSIZE(stereoGraphModes)))
            {
                xrView.stereoGraphMode = 0u;
                changed                = true;
            }

            changed |= ImGui::Checkbox("Fallback Mono", &xrView.fallbackMono);

            if (auto* backend = ctx.services ? ctx.services->tryGet<vultra::IRenderBackendService>() : nullptr)
            {
                ImGui::SeparatorText("Runtime");
                ImGui::Text("OpenXR: %s", backend->isXREnabled() ? "Enabled" : "Disabled");
                ImGui::Text("Mirror: %s", backend->isXRMirrorEnabled() ? "Enabled" : "Disabled");
                ImGui::BeginDisabled(!xrView.enabled);
                if (ImGui::SmallButton(ICON_MDI_HEADSET "  Request XR Session"))
                {
                    backend->requestXRSession(false);
                    backend->requestXRSession(true);
                }
                ImGui::EndDisabled();
                if (!backend->isXREnabled())
                    ImGui::TextDisabled("Session starts when an enabled XR camera is active.");
            }

            return changed;
        }

        glm::mat4 makeTransformMatrix(const vultra::TransformComponent& transform)
        {
            return glm::translate(glm::mat4 {1.0f}, transform.position) * glm::mat4_cast(transform.rotation) *
                   glm::scale(glm::mat4 {1.0f}, transform.scale);
        }

        glm::mat4 makeWorldTransformMatrix(const entt::registry& reg, const entt::entity entity)
        {
            const auto* transform = reg.try_get<vultra::TransformComponent>(entity);
            if (!transform)
                return glm::mat4 {1.0f};

            const auto  local     = makeTransformMatrix(*transform);
            const auto* hierarchy = reg.try_get<vultra::HierarchyComponent>(entity);
            if (!hierarchy || hierarchy->parent == entt::null || !reg.valid(hierarchy->parent))
                return local;

            return makeWorldTransformMatrix(reg, hierarchy->parent) * local;
        }

        struct Bounds
        {
            glm::vec3 min {std::numeric_limits<float>::max()};
            glm::vec3 max {std::numeric_limits<float>::lowest()};
            bool      valid {false};

            void include(const glm::vec3& p)
            {
                min   = valid ? glm::min(min, p) : p;
                max   = valid ? glm::max(max, p) : p;
                valid = true;
            }
        };

        void includeMeshLocalBounds(Bounds& bounds, const vasset::VMesh& mesh)
        {
            if (mesh.hasLocalBounds)
            {
                bounds.include(mesh.localBoundsMin);
                bounds.include(mesh.localBoundsMax);
                return;
            }

            for (const auto& p : mesh.positions)
                bounds.include(p);
        }

        void includeMeshWorldBounds(Bounds& bounds, const vasset::VMesh& mesh, const glm::mat4& worldMatrix)
        {
            if (mesh.hasLocalBounds)
            {
                const glm::vec3 min = mesh.localBoundsMin;
                const glm::vec3 max = mesh.localBoundsMax;
                for (uint32_t corner = 0; corner < 8u; ++corner)
                {
                    const glm::vec3 p {
                        (corner & 1u) ? max.x : min.x,
                        (corner & 2u) ? max.y : min.y,
                        (corner & 4u) ? max.z : min.z,
                    };
                    bounds.include(glm::vec3(worldMatrix * glm::vec4(p, 1.0f)));
                }
                return;
            }

            for (const auto& p : mesh.positions)
                bounds.include(glm::vec3(worldMatrix * glm::vec4(p, 1.0f)));
        }

        Bounds computeLocalMeshBounds(EditorContext& ctx, entt::registry& reg, entt::entity entity)
        {
            Bounds bounds;
            const auto* meshComponent = reg.try_get<vultra::MeshComponent>(entity);
            if (!meshComponent)
                return bounds;

            if (meshComponent->builtinGeometry != UINT32_MAX)
            {
                switch (meshComponent->builtinGeometry)
                {
                    case 0u: // Quad
                        bounds.include({-0.5f, 0.0f, -0.5f});
                        bounds.include({0.5f, 0.0f, 0.5f});
                        return bounds;
                    case 1u: // Cube
                    case 2u: // Sphere
                        bounds.include({-0.5f, -0.5f, -0.5f});
                        bounds.include({0.5f, 0.5f, 0.5f});
                        return bounds;
                    case 3u: // Capsule
                        bounds.include({-0.5f, -1.0f, -0.5f});
                        bounds.include({0.5f, 1.0f, 0.5f});
                        return bounds;
                    default:
                        break;
                }
            }

            if (!meshComponent->mesh.valid())
                return bounds;

            auto* assets = ctx.services ? ctx.services->tryGet<vultra::IAssetService>() : nullptr;
            if (!assets)
                return bounds;

            auto mesh = assets->loadMeshAsync(meshComponent->mesh);
            if (!mesh.cpu())
                return bounds;

            includeMeshLocalBounds(bounds, *mesh.cpu());
            return bounds;
        }

        void fitBoxShapeToMeshBounds(EditorContext& ctx, entt::registry& reg, entt::entity entity)
        {
            auto& shape = reg.get_or_emplace<vultra::BoxShapeComponent>(entity);
            const auto bounds = computeLocalMeshBounds(ctx, reg, entity);
            if (!bounds.valid)
                return;

            shape.halfExtents = glm::max((bounds.max - bounds.min) * 0.5f, glm::vec3 {0.001f});
        }

        void fitSphereShapeToMeshBounds(EditorContext& ctx, entt::registry& reg, entt::entity entity)
        {
            auto& shape = reg.get_or_emplace<vultra::SphereShapeComponent>(entity);
            const auto bounds = computeLocalMeshBounds(ctx, reg, entity);
            if (!bounds.valid)
                return;

            shape.radius = std::max(glm::length(bounds.max - bounds.min) * 0.5f, 0.001f);
        }

        void fitCapsuleShapeToMeshBounds(EditorContext& ctx, entt::registry& reg, entt::entity entity)
        {
            auto& shape = reg.get_or_emplace<vultra::CapsuleShapeComponent>(entity);
            const auto bounds = computeLocalMeshBounds(ctx, reg, entity);
            if (!bounds.valid)
                return;

            const glm::vec3 size = glm::max(bounds.max - bounds.min, glm::vec3 {0.001f});
            shape.radius = std::max(std::max(size.x, size.z) * 0.5f, 0.001f);
            shape.halfHeightOfCylinder = std::max((size.y * 0.5f) - shape.radius, 0.001f);
        }

        entt::entity findNamedEntity(vultra::World& world, const std::string& name)
        {
            auto& reg  = world.registry();
            auto  view = reg.view<vultra::NameComponent>();
            for (auto e : view)
            {
                if (view.get<vultra::NameComponent>(e).name == name)
                    return e;
            }
            return entt::null;
        }

        bool isDescendantOrSelf(const vultra::World& world, entt::entity entity, entt::entity root)
        {
            for (auto e = entity; e != entt::null; e = world.parent(e))
            {
                if (e == root)
                    return true;
            }
            return false;
        }

        Bounds computeEntitySubtreeMeshBounds(vultra::World& world, vultra::IAssetService& assets, entt::entity root)
        {
            Bounds bounds;
            if (root == entt::null)
                return bounds;

            auto& reg  = world.registry();
            auto  view = reg.view<vultra::TransformComponent, vultra::MeshComponent>();
            for (auto e : view)
            {
                if (!isDescendantOrSelf(world, e, root))
                    continue;

                const auto& meshComponent = view.get<vultra::MeshComponent>(e);
                if (!meshComponent.mesh.valid())
                    continue;

                auto mesh = assets.loadMeshAsync(meshComponent.mesh);
                if (!mesh.cpu())
                    continue;

                const auto worldMatrix = makeWorldTransformMatrix(reg, e);
                includeMeshWorldBounds(bounds, *mesh.cpu(), worldMatrix);
            }
            return bounds;
        }

        Bounds computeWorldMeshBounds(vultra::World& world, vultra::IAssetService& assets)
        {
            Bounds bounds;
            auto&  reg  = world.registry();
            auto   view = reg.view<vultra::TransformComponent, vultra::MeshComponent>();
            for (auto e : view)
            {
                const auto& meshComponent = view.get<vultra::MeshComponent>(e);
                if (!meshComponent.mesh.valid())
                    continue;
                auto mesh = assets.loadMeshAsync(meshComponent.mesh);
                if (!mesh.cpu())
                    continue;
                const auto worldMatrix = makeWorldTransformMatrix(reg, e);
                includeMeshWorldBounds(bounds, *mesh.cpu(), worldMatrix);
            }
            return bounds;
        }

        bool previewWorldAssetsReady(vultra::World& world, vultra::IAssetService& assets)
        {
            bool hasPreviewAsset = false;
            auto& reg = world.registry();
            auto view = reg.view<vultra::MeshComponent>();
            for (auto e : view)
            {
                (void)e;
                const auto& meshComponent = view.get<vultra::MeshComponent>(e);
                if (meshComponent.builtinGeometry != UINT32_MAX)
                    continue;
                if (!meshComponent.mesh.valid())
                    return false;
                hasPreviewAsset = true;
                if (!assets.meshPreviewReady(meshComponent.mesh))
                    return false;
            }

            return !hasPreviewAsset || !assets.materialRefreshPending();
        }

        void updateWorldTransforms(vultra::World& world)
        {
            auto&                     reg = world.registry();
            std::vector<entt::entity> roots;
            auto                      view = reg.view<vultra::TransformComponent, vultra::HierarchyComponent>();
            roots.reserve(view.size_hint());
            for (auto e = world.firstChild(entt::null); e != entt::null; e = world.nextSibling(e))
            {
                if (reg.all_of<vultra::TransformComponent, vultra::HierarchyComponent>(e))
                    roots.push_back(e);
            }

            struct StackItem
            {
                entt::entity e {entt::null};
                glm::mat4    parentWorld {1.0f};
                bool         parentDirty {false};
            };
            std::vector<StackItem> stack;
            for (auto root : roots)
                stack.push_back({root, glm::mat4 {1.0f}, true});
            while (!stack.empty())
            {
                const auto item = stack.back();
                stack.pop_back();
                auto* t = reg.try_get<vultra::TransformComponent>(item.e);
                auto* h = reg.try_get<vultra::HierarchyComponent>(item.e);
                if (!t || !h)
                    continue;
                const bool dirty = t->dirty || item.parentDirty;
                if (dirty)
                {
                    t->worldMatrix = item.parentWorld * makeTransformMatrix(*t);
                    t->dirty       = false;
                }
                for (auto child = h->firstChild; child != entt::null; child = world.nextSibling(child))
                    stack.push_back({child, t->worldMatrix, dirty});
            }
        }

        void addPreviewLighting(vultra::World& world)
        {
            auto& reg      = world.registry();
            auto  keyLight = world.createEntity();
            reg.emplace<vultra::NameComponent>(keyLight, vultra::NameComponent {"Preview Key Light"});
            reg.emplace<vultra::LightComponent>(keyLight,
                                                vultra::LightComponent {
                                                    .kind        = 0u,
                                                    .color       = glm::vec3 {1.0f},
                                                    .intensity   = 6.0f,
                                                    .castsShadow = false,
                                                });

            auto env = world.createEntity();
            reg.emplace<vultra::NameComponent>(env, vultra::NameComponent {"Preview Environment"});
            reg.emplace<vultra::EnvironmentComponent>(env,
                                                      vultra::EnvironmentComponent {
                                                          .ambientColor     = glm::vec3 {0.28f, 0.30f, 0.34f},
                                                          .ambientIntensity = 1.4f,
                                                          .enableIBL        = false,
                                                      });
        }

        entt::entity findNamedEntity(vultra::World& world, const char* name)
        {
            auto& reg  = world.registry();
            auto  view = reg.view<vultra::NameComponent>();
            for (auto e : view)
            {
                if (view.get<vultra::NameComponent>(e).name == name)
                    return e;
            }
            return entt::null;
        }

        void setPreviewDirectionalLight(vultra::World&   world,
                                        const char*      name,
                                        glm::vec3        direction,
                                        const glm::vec3& fallback)
        {
            auto&      reg    = world.registry();
            const auto entity = findNamedEntity(world, name);
            if (entity == entt::null || !reg.valid(entity) || !reg.all_of<vultra::TransformComponent>(entity))
                return;

            const float len2 = glm::dot(direction, direction);
            if (len2 <= 1e-8f)
                direction = fallback;
            else
                direction *= glm::inversesqrt(len2);

            glm::vec3 up {0.0f, 1.0f, 0.0f};
            if (std::abs(glm::dot(up, direction)) > 0.95f)
                up = glm::vec3 {1.0f, 0.0f, 0.0f};

            auto& transform    = reg.get<vultra::TransformComponent>(entity);
            transform.rotation = glm::normalize(glm::quatLookAtRH(direction, up));
            transform.dirty    = true;
        }

        void centerPreviewContent(vultra::World& world, vultra::IAssetService& assets, const entt::entity contentRoot)
        {
            auto& reg = world.registry();
            if (contentRoot == entt::null || !reg.valid(contentRoot) ||
                !reg.all_of<vultra::TransformComponent>(contentRoot))
            {
                return;
            }

            updateWorldTransforms(world);
            const auto bounds = computeWorldMeshBounds(world, assets);
            if (!bounds.valid)
                return;

            auto& transform = reg.get<vultra::TransformComponent>(contentRoot);
            transform.position -= (bounds.min + bounds.max) * 0.5f;
            transform.dirty = true;
            updateWorldTransforms(world);
        }

        glm::vec3 mapPreviewArcballPoint(const ImVec2& mouse, const ImVec2& min, const ImVec2& max)
        {
            const float width    = std::max(1.0f, max.x - min.x);
            const float height   = std::max(1.0f, max.y - min.y);
            const float diameter = std::max(1.0f, std::min(width, height));
            const float x        = (2.0f * (mouse.x - (min.x + width * 0.5f))) / diameter;
            const float y        = (-2.0f * (mouse.y - (min.y + height * 0.5f))) / diameter;
            const float len2     = x * x + y * y;

            if (len2 <= 1.0f)
                return glm::normalize(glm::vec3 {x, y, std::sqrt(std::max(0.0f, 1.0f - len2))});

            const float invLen = 1.0f / std::sqrt(len2);
            return glm::vec3 {x * invLen, y * invLen, 0.0f};
        }

        glm::quat arcballDelta(const glm::vec3& from, const glm::vec3& to)
        {
            const glm::vec3 axis     = glm::cross(from, to);
            const float     axisLen2 = glm::dot(axis, axis);
            if (axisLen2 <= 1e-8f)
                return glm::quat {1.0f, 0.0f, 0.0f, 0.0f};

            const float dot = std::clamp(glm::dot(from, to), -1.0f, 1.0f);
            return glm::normalize(glm::angleAxis(std::acos(dot), axis * glm::inversesqrt(axisLen2)));
        }

        uint32_t quantizePreviewExtent(const float size)
        {
            constexpr uint32_t kStep  = 32u;
            const auto         pixels = static_cast<uint32_t>(std::ceil(std::max(1.0f, size)));
            return std::max(kStep, ((pixels + kStep - 1u) / kStep) * kStep);
        }

        bool decomposeTransformMatrix(const glm::mat4& matrix, vultra::TransformComponent& transform)
        {
            glm::vec3 skew {};
            glm::vec4 perspective {};
            glm::quat rotation {};
            glm::vec3 translation {};
            glm::vec3 scale {};
            if (!glm::decompose(matrix, scale, rotation, translation, skew, perspective))
                return false;

            transform.position = translation;
            transform.rotation = glm::normalize(glm::conjugate(rotation));
            transform.scale    = scale;
            transform.dirty    = true;
            return true;
        }

        glm::quat extractRotation(const glm::mat4& matrix)
        {
            glm::mat3 basis {matrix};
            for (int i = 0; i < 3; ++i)
            {
                const float len2 = glm::dot(basis[i], basis[i]);
                if (len2 > 1e-8f)
                    basis[i] *= glm::inversesqrt(len2);
            }
            return glm::normalize(glm::quat_cast(basis));
        }

        bool alignCameraEntityToSceneView(EditorContext& ctx, vultra::World& world, entt::entity entity)
        {
            if (!ctx.state.sceneCamera.valid)
            {
                ctx.state.statusMessage = "Scene View camera is not available yet.";
                return false;
            }

            auto& reg = world.registry();
            if (!reg.all_of<vultra::CameraComponent>(entity))
                return false;

            auto& transform = reg.get_or_emplace<vultra::TransformComponent>(entity);
            auto& camera    = reg.get<vultra::CameraComponent>(entity);

            if (auto* hierarchy = reg.try_get<vultra::HierarchyComponent>(entity);
                hierarchy && hierarchy->parent != entt::null && reg.valid(hierarchy->parent) &&
                reg.all_of<vultra::TransformComponent>(hierarchy->parent))
            {
                const auto parentWorld = makeWorldTransformMatrix(reg, hierarchy->parent);
                transform.position =
                    glm::vec3(glm::inverse(parentWorld) * glm::vec4(ctx.state.sceneCamera.position, 1.0f));
                transform.rotation =
                    glm::normalize(glm::inverse(extractRotation(parentWorld)) * ctx.state.sceneCamera.rotation);
                transform.dirty = true;
            }
            else
            {
                transform.position = ctx.state.sceneCamera.position;
                transform.rotation = ctx.state.sceneCamera.rotation;
                transform.dirty    = true;
            }

            if (camera.projection == 0u)
                camera.fovYDegrees = ctx.state.sceneCamera.fovYDegrees;
            ctx.state.statusMessage = "Camera aligned to Scene View.";
            return true;
        }

        bool alignSceneViewToCameraEntity(EditorContext& ctx, vultra::World& world, entt::entity entity)
        {
            auto& reg = world.registry();
            if (!reg.all_of<vultra::TransformComponent, vultra::CameraComponent>(entity))
                return false;

            const auto worldTransform = makeWorldTransformMatrix(reg, entity);

            const auto& camera                         = reg.get<vultra::CameraComponent>(entity);
            ctx.state.sceneCameraAlignRequest.pending  = true;
            ctx.state.sceneCameraAlignRequest.position = glm::vec3(worldTransform[3]);
            ctx.state.sceneCameraAlignRequest.rotation = extractRotation(worldTransform);
            ctx.state.sceneCameraAlignRequest.fovYDegrees =
                camera.projection == 0u ? camera.fovYDegrees : ctx.state.sceneCamera.fovYDegrees;
            ctx.state.statusMessage = "Scene View aligned to Camera.";
            return true;
        }

        const char* metaFieldNameFromId(const entt::id_type id)
        {
            auto is = [id](const char* name) { return id == entt::hashed_string {name}.value(); };

            if (is("uuid"))
                return "uuid";
            if (is("name"))
                return "name";
            if (is("active"))
                return "active";
            if (is("visible"))
                return "visible";
            if (is("locked"))
                return "locked";
            if (is("selectable"))
                return "selectable";
            if (is("position"))
                return "position";
            if (is("rotation"))
                return "rotation";
            if (is("scale"))
                return "scale";
            if (is("motionType"))
                return "motionType";
            if (is("objectLayer"))
                return "objectLayer";
            if (is("isSensor"))
                return "isSensor";
            if (is("motionQuality"))
                return "motionQuality";
            if (is("allowSleeping"))
                return "allowSleeping";
            if (is("friction"))
                return "friction";
            if (is("restitution"))
                return "restitution";
            if (is("linearDamping"))
                return "linearDamping";
            if (is("angularDamping"))
                return "angularDamping";
            if (is("gravityFactor"))
                return "gravityFactor";
            if (is("linearVelocity"))
                return "linearVelocity";
            if (is("angularVelocity"))
                return "angularVelocity";
            if (is("mass"))
                return "mass";
            if (is("overrideMass"))
                return "overrideMass";
            if (is("maxLinearVelocity"))
                return "maxLinearVelocity";
            if (is("maxAngularVelocity"))
                return "maxAngularVelocity";
            if (is("halfExtents"))
                return "halfExtents";
            if (is("halfHeightOfCylinder"))
                return "halfHeightOfCylinder";
            if (is("mesh"))
                return "mesh";
            if (is("gaussianSplat"))
                return "gaussianSplat";
            if (is("primary"))
                return "primary";
            if (is("projection"))
                return "projection";
            if (is("fovYDegrees"))
                return "fovYDegrees";
            if (is("orthographicHeight"))
                return "orthographicHeight";
            if (is("zNear"))
                return "zNear";
            if (is("zFar"))
                return "zFar";
            if (is("clearMode"))
                return "clearMode";
            if (is("clearColor"))
                return "clearColor";
            if (is("priority"))
                return "priority";
            if (is("rendererKey"))
                return "rendererKey";
            if (is("skybox"))
                return "skybox";
            if (is("ambientColor"))
                return "ambientColor";
            if (is("ambientIntensity"))
                return "ambientIntensity";
            if (is("enableIBL"))
                return "enableIBL";
            if (is("iblColor"))
                return "iblColor";
            if (is("iblIntensity"))
                return "iblIntensity";
            if (is("environmentMap"))
                return "environmentMap";
            if (is("shape"))
                return "shape";
            if (is("boxSize"))
                return "boxSize";
            if (is("blendDistance"))
                return "blendDistance";
            if (is("parallaxCorrection"))
                return "parallaxCorrection";
            if (is("kind"))
                return "kind";
            if (is("color"))
                return "color";
            if (is("intensity"))
                return "intensity";
            if (is("range"))
                return "range";
            if (is("radius"))
                return "radius";
            if (is("width"))
                return "width";
            if (is("height"))
                return "height";
            if (is("innerConeDegrees"))
                return "innerConeDegrees";
            if (is("outerConeDegrees"))
                return "outerConeDegrees";
            if (is("castsShadow"))
                return "castsShadow";
            if (is("twoSided"))
                return "twoSided";
            if (is("scriptUri"))
                return "scriptUri";
            if (is("enabled"))
                return "enabled";

            return nullptr;
        }

        vasset::VAssetType expectedAssetTypeForField(const char* fieldName)
        {
            if (std::strcmp(fieldName, "mesh") == 0)
                return vasset::VAssetType::eMesh;
            if (std::strcmp(fieldName, "skeleton") == 0)
                return vasset::VAssetType::eSkeleton;
            if (std::strcmp(fieldName, "animation") == 0)
                return vasset::VAssetType::eAnimation;
            if (std::strcmp(fieldName, "gaussianSplat") == 0)
                return vasset::VAssetType::eGaussianSplat;
            if (std::strcmp(fieldName, "skybox") == 0)
                return vasset::VAssetType::eTexture;
            if (std::strcmp(fieldName, "environmentMap") == 0)
                return vasset::VAssetType::eTexture;
            return vasset::VAssetType::eUnknown;
        }

        const char* expectedAssetLabelForField(const char* fieldName)
        {
            const auto type = expectedAssetTypeForField(fieldName);
            if (type == vasset::VAssetType::eMesh)
                return "Mesh";
            if (type == vasset::VAssetType::eSkeleton)
                return "Skeleton";
            if (type == vasset::VAssetType::eAnimation)
                return "Animation";
            if (type == vasset::VAssetType::eGaussianSplat)
                return "Gaussian Splat";
            if (type == vasset::VAssetType::eTexture)
                return "Texture";
            return "Asset";
        }

        bool isAcceptedAssetUuid(EditorContext* ctx, const vultra::CoreUUID& uuid, const char* fieldName)
        {
            if (!uuid.valid())
                return false;

            const auto expectedType = expectedAssetTypeForField(fieldName);
            if (expectedType == vasset::VAssetType::eUnknown)
                return true;

            auto* assetService = ctx && ctx->services ? ctx->services->tryGet<vultra::IAssetService>() : nullptr;
            if (!assetService)
                return false;

            return assetService->registry().lookup(uuid.native()).type == expectedType;
        }

        std::filesystem::path editorAssetRoot(EditorContext& ctx)
        {
            return (ctx.state.currentProject / ctx.state.currentAssetRoot).lexically_normal();
        }

        std::string pathToResUri(EditorContext& ctx, const std::filesystem::path& path)
        {
            const auto      assetRoot = editorAssetRoot(ctx);
            std::error_code ec;
            const auto      rel = std::filesystem::relative(path, assetRoot, ec);
            if (ec || rel.empty())
                return {};
            return "res://" + rel.generic_string();
        }

        struct RenderGraphPassEditState
        {
            std::filesystem::path path;
            std::array<char, 128> type {};
            std::array<char, 256> inputs {};
            std::array<char, 256> outputs {};
            std::array<char, 128> library {};
            std::array<char, 128> vertexLibrary {};
            std::array<char, 128> fragmentLibrary {};
            std::array<char, 128> vertex {};
            std::array<char, 128> fragment {};
            std::array<char, 128> compute {};
            std::array<char, 128> raygen {};
            std::array<char, 128> miss {};
            std::array<char, 128> closestHit {};
            std::array<char, 128> anyHit {};
            int                   pipeline {0};
            bool                  dispatchByOutputSize {false};
            bool                  valid {false};
        };

        std::string solString(sol::table table, const char* key, std::string fallback = {})
        {
            sol::object value = table[key];
            return value.is<std::string>() ? value.as<std::string>() : std::move(fallback);
        }

        std::string solStringListText(sol::table table, const char* key, const char* fallback)
        {
            sol::object value = table[key];
            if (value.is<std::string>())
                return value.as<std::string>();
            if (!value.is<sol::table>())
                return fallback;

            std::string out;
            sol::table  values = value.as<sol::table>();
            for (const auto& [_, item] : values)
            {
                static_cast<void>(_);
                if (!item.is<std::string>())
                    continue;
                if (!out.empty())
                    out += ", ";
                out += item.as<std::string>();
            }
            return out.empty() ? fallback : out;
        }

        bool fileLooksLikeRenderGraphPass(const std::filesystem::path& path)
        {
            if (path.extension() != ".lua")
                return false;
            std::ifstream file(path);
            if (!file.is_open())
                return false;
            std::stringstream buffer;
            buffer << file.rdbuf();
            return buffer.str().find("RenderGraphPass") != std::string::npos;
        }

        std::optional<RenderGraphPassEditState> loadRenderGraphPassEditState(const std::filesystem::path& path)
        {
            std::ifstream file(path);
            if (!file.is_open())
                return std::nullopt;

            std::stringstream buffer;
            buffer << file.rdbuf();
            const auto text = buffer.str();
            if (text.find("RenderGraphPass") == std::string::npos)
                return std::nullopt;

            sol::state lua;
            lua.open_libraries(sol::lib::base, sol::lib::table, sol::lib::string, sol::lib::math);
            lua.set_function("RenderGraphPass", [](sol::table t) { return t; });
            auto result = lua.safe_script(text, &sol::script_pass_on_error);
            if (!result.valid())
                return std::nullopt;

            sol::object obj = result;
            if (!obj.is<sol::table>())
                return std::nullopt;

            sol::table table = obj.as<sol::table>();
            RenderGraphPassEditState state;
            state.path = path.lexically_normal();
            copyName(state.type, solString(table, "type", solString(table, "name", "CustomPass")));
            copyName(state.inputs, solStringListText(table, "inputs", solString(table, "input", "source").c_str()));
            copyName(state.outputs, solStringListText(table, "outputs", solString(table, "output", "color").c_str()));

            const auto pipeline = solString(table, "pipeline", solString(table, "stage", "graphics"));
            if (pipeline == "compute")
                state.pipeline = 1;
            else if (pipeline == "raytracing" || pipeline == "ray_tracing" || pipeline == "rt")
                state.pipeline = 2;

            if (sol::object shaderObj = table["shader"]; shaderObj.is<sol::table>())
            {
                sol::table shader = shaderObj.as<sol::table>();
                copyName(state.library, solString(shader, "library", "project"));
                copyName(state.vertex, solString(shader, "vertex", "fullscreen_triangle.vert"));
                copyName(state.fragment, solString(shader, "fragment"));
                const std::string defaultVertexLibrary =
                    std::string(state.vertex.data()) == "fullscreen_triangle.vert" ? "builtin" : state.library.data();
                copyName(state.vertexLibrary,
                         solString(shader, "vertexLibrary", solString(shader, "vertex_library", defaultVertexLibrary.c_str())));
                copyName(state.fragmentLibrary,
                         solString(shader, "fragmentLibrary", solString(shader, "fragment_library", state.library.data())));
                copyName(state.compute, solString(shader, "compute"));
                copyName(state.raygen, solString(shader, "raygen"));
                copyName(state.miss, solString(shader, "miss"));
                copyName(state.closestHit, solString(shader, "closestHit"));
                copyName(state.anyHit, solString(shader, "anyHit"));
            }
            else
            {
                copyName(state.library, "project");
                copyName(state.vertexLibrary, "builtin");
                copyName(state.fragmentLibrary, "project");
                copyName(state.vertex, "fullscreen_triangle.vert");
            }

            if (sol::object dispatchObj = table["dispatch"]; dispatchObj.is<sol::table>())
            {
                sol::table dispatch = dispatchObj.as<sol::table>();
                sol::object bySize  = dispatch["byOutputSize"];
                state.dispatchByOutputSize = bySize.is<bool>() && bySize.as<bool>();
            }

            state.valid = true;
            return state;
        }

        std::vector<std::string> commaList(std::string_view value, std::string_view fallback)
        {
            std::vector<std::string> out;
            std::stringstream        ss {std::string(value)};
            std::string              item;
            while (std::getline(ss, item, ','))
            {
                item.erase(item.begin(),
                           std::find_if(item.begin(), item.end(), [](unsigned char ch) { return !std::isspace(ch); }));
                item.erase(std::find_if(item.rbegin(),
                                        item.rend(),
                                        [](unsigned char ch) { return !std::isspace(ch); })
                               .base(),
                           item.end());
                if (!item.empty())
                    out.push_back(std::move(item));
            }
            if (out.empty() && !fallback.empty())
                out.emplace_back(fallback);
            return out;
        }

        std::string quoteLua(std::string_view value)
        {
            std::string out = "\"";
            for (const char ch : value)
            {
                if (ch == '\\' || ch == '"')
                    out.push_back('\\');
                out.push_back(ch);
            }
            out.push_back('"');
            return out;
        }

        std::string luaStringArray(const std::vector<std::string>& values)
        {
            std::string out = "{ ";
            for (size_t i = 0; i < values.size(); ++i)
            {
                if (i > 0)
                    out += ", ";
                out += quoteLua(values[i]);
            }
            out += " }";
            return out;
        }

        std::string serializeRenderGraphPass(const RenderGraphPassEditState& state)
        {
            std::string out;
            out += "return RenderGraphPass {\n";
            out += "    type = " + quoteLua(state.type.data()) + ",\n";
            if (state.pipeline == 1)
                out += "    pipeline = \"compute\",\n";
            else if (state.pipeline == 2)
                out += "    pipeline = \"raytracing\",\n";
            out += "    inputs = " + luaStringArray(commaList(state.inputs.data(), "source")) + ",\n";
            out += "    outputs = " + luaStringArray(commaList(state.outputs.data(), "color")) + ",\n";
            out += "    shader = {\n";
            if (state.pipeline == 1)
            {
                out += "        library = " + quoteLua(state.library.data()[0] ? state.library.data() : "project") + ",\n";
                out += "        compute = " + quoteLua(state.compute.data()) + ",\n";
            }
            else if (state.pipeline == 2)
            {
                out += "        library = " + quoteLua(state.library.data()[0] ? state.library.data() : "project") + ",\n";
                out += "        raygen = " + quoteLua(state.raygen.data()) + ",\n";
                if (state.miss[0] != '\0')
                    out += "        miss = " + quoteLua(state.miss.data()) + ",\n";
                if (state.closestHit[0] != '\0')
                    out += "        closestHit = " + quoteLua(state.closestHit.data()) + ",\n";
                if (state.anyHit[0] != '\0')
                    out += "        anyHit = " + quoteLua(state.anyHit.data()) + ",\n";
            }
            else
            {
                const std::string vertexLibrary = state.vertexLibrary.data()[0] ? state.vertexLibrary.data() :
                                                                                  "builtin";
                const std::string fragmentLibrary = state.fragmentLibrary.data()[0] ? state.fragmentLibrary.data() :
                                                                                       "project";
                const std::string vertexShader =
                    vertexLibrary == "builtin" ? "fullscreen_triangle.vert" :
                                                 (state.vertex.data()[0] ? state.vertex.data() :
                                                                           "fullscreen_triangle.vert");
                if (vertexLibrary != "project")
                    out += "        vertexLibrary = " + quoteLua(vertexLibrary) + ",\n";
                if (!fragmentLibrary.empty() && fragmentLibrary != "project")
                    out += "        fragmentLibrary = " + quoteLua(fragmentLibrary) + ",\n";
                out += "        vertex = " + quoteLua(vertexShader) + ",\n";
                out += "        fragment = " + quoteLua(state.fragment.data()) + ",\n";
            }
            out += "    },\n";
            if (state.pipeline == 1)
            {
                out += "    dispatch = {\n";
                out += std::string("        byOutputSize = ") + (state.dispatchByOutputSize ? "true" : "false") + ",\n";
                out += "    },\n";
            }
            out += "}\n";
            return out;
        }

        bool shaderFileHasStage(const std::filesystem::path& path, const std::string& stage)
        {
            const auto suffix = "." + stage + ".vshader";
            if (path.filename().generic_string().ends_with(suffix))
                return true;

            std::ifstream file(path);
            if (!file.is_open())
                return false;

            std::string line;
            while (std::getline(file, line))
            {
                line.erase(std::remove_if(line.begin(),
                                          line.end(),
                                          [](unsigned char ch) { return std::isspace(ch) != 0; }),
                           line.end());
                if (line == "[" + stage + "]")
                    return true;
            }
            return false;
        }

        std::vector<std::string> collectProjectShaderIds(EditorContext& ctx,
                                                         const std::string& stage,
                                                         std::string_view current,
                                                         const bool includeCurrent = true)
        {
            std::vector<std::pair<std::string, std::string>> shaders;
            const auto               root = (editorAssetRoot(ctx) / "shaders").lexically_normal();
            std::error_code          ec;
            if (std::filesystem::is_directory(root, ec))
            {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec))
                {
                    if (ec)
                        break;
                    if (!entry.is_regular_file(ec) || entry.path().extension() != ".vshader")
                        continue;
                    if (!shaderFileHasStage(entry.path(), stage))
                        continue;

                    auto rel = std::filesystem::relative(entry.path(), root, ec).generic_string();
                    if (ec)
                        continue;
                    if (rel == "generated" || rel.starts_with("generated/"))
                        continue;
                    if (rel.ends_with(".vshader"))
                        rel.resize(rel.size() - std::string_view(".vshader").size());
                    auto leaf = std::filesystem::path(rel).filename().generic_string();
                    shaders.emplace_back(std::move(rel), std::move(leaf));
                }
            }

            std::unordered_map<std::string, int> leafCounts;
            for (const auto& [_, leaf] : shaders)
            {
                static_cast<void>(_);
                ++leafCounts[leaf];
            }

            std::vector<std::string> out;
            out.reserve(shaders.size() + 1);
            for (const auto& [rel, leaf] : shaders)
                out.push_back(leafCounts[leaf] > 1 ? rel : leaf);

            if (includeCurrent && !current.empty() && std::find(out.begin(), out.end(), current) == out.end())
                out.emplace_back(current);

            std::sort(out.begin(), out.end());
            out.erase(std::unique(out.begin(), out.end()), out.end());
            return out;
        }

        std::vector<std::string> collectBuiltinShaderIds(const std::string& stage)
        {
            std::vector<std::pair<std::string, std::string>> shaders;
            const auto suffix = "." + stage + ".vshader";
            const auto root   = std::filesystem::current_path() / "builtin" / "shaders" / "passes";
            std::error_code ec;
            if (std::filesystem::is_directory(root, ec))
            {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec))
                {
                    if (ec)
                        break;
                    if (!entry.is_regular_file(ec) || !entry.path().filename().generic_string().ends_with(suffix))
                        continue;

                    auto rel = std::filesystem::relative(entry.path(), root, ec).generic_string();
                    if (ec)
                        continue;
                    if (rel.ends_with(".vshader"))
                        rel.resize(rel.size() - std::string_view(".vshader").size());
                    auto leaf = std::filesystem::path(rel).filename().generic_string();
                    shaders.emplace_back(std::move(rel), std::move(leaf));
                }
            }

            std::unordered_map<std::string, int> leafCounts;
            for (const auto& [_, leaf] : shaders)
            {
                static_cast<void>(_);
                ++leafCounts[leaf];
            }

            std::vector<std::string> out;
            out.reserve(shaders.size());
            for (const auto& [rel, leaf] : shaders)
                out.push_back(leafCounts[leaf] > 1 ? rel : leaf);
            std::sort(out.begin(), out.end());
            out.erase(std::unique(out.begin(), out.end()), out.end());
            return out;
        }

        std::vector<std::string>
        collectShaderIdsForLibrary(EditorContext& ctx, std::string_view library, const std::string& stage)
        {
            return library == "builtin" ? collectBuiltinShaderIds(stage) : collectProjectShaderIds(ctx, stage, "", false);
        }

        bool drawShaderOptionSelector(const char*                  label,
                                      std::array<char, 128>&       value,
                                      const std::vector<std::string>& options,
                                      const bool                   allowEmpty = false)
        {
            bool changed = false;
            ImGui::PushID(label);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::SameLine(120.0f);
            ImGui::SetNextItemWidth(280.0f);
            const char* preview = value[0] == '\0' ? "<none>" : value.data();
            if (ImGui::BeginCombo("##shader", preview))
            {
                if (allowEmpty && ImGui::Selectable("<none>", value[0] == '\0'))
                {
                    value.fill('\0');
                    changed = true;
                }
                for (const auto& option : options)
                {
                    const bool selected = option == value.data();
                    if (ImGui::Selectable(option.c_str(), selected))
                    {
                        copyName(value, option);
                        changed = true;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::PopID();
            return changed;
        }

        bool normalizeShaderForLibrary(EditorContext&          ctx,
                                       std::array<char, 128>&  library,
                                       const std::string&      stage,
                                       std::array<char, 128>&  shader,
                                       const bool              allowEmpty = false)
        {
            if (allowEmpty && shader[0] == '\0')
                return false;

            bool changed = false;
            if (library[0] == '\0')
            {
                copyName(library, "project");
                changed = true;
            }

            auto options = collectShaderIdsForLibrary(ctx, library.data(), stage);
            if (std::find(options.begin(), options.end(), shader.data()) != options.end())
                return changed;

            const auto otherLibrary = std::string_view(library.data()) == "builtin" ? "project" : "builtin";
            auto       otherOptions = collectShaderIdsForLibrary(ctx, otherLibrary, stage);
            if (std::find(otherOptions.begin(), otherOptions.end(), shader.data()) != otherOptions.end())
            {
                copyName(library, otherLibrary);
                return true;
            }

            if (!options.empty())
            {
                copyName(shader, options.front());
                return true;
            }

            if (!otherOptions.empty())
            {
                copyName(library, otherLibrary);
                copyName(shader, otherOptions.front());
                return true;
            }

            if (allowEmpty)
            {
                shader.fill('\0');
                return true;
            }

            return changed;
        }

        bool drawLibraryShaderSelector(EditorContext&          ctx,
                                       const char*             label,
                                       const std::string&      stage,
                                       std::array<char, 128>&  library,
                                       std::array<char, 128>&  shader,
                                       const bool              allowEmpty = false)
        {
            normalizeShaderForLibrary(ctx, library, stage, shader, allowEmpty);
            const auto options = collectShaderIdsForLibrary(ctx, library.data()[0] ? library.data() : "project", stage);
            return drawShaderOptionSelector(label, shader, options, allowEmpty);
        }

        bool drawShaderSelector(EditorContext&          ctx,
                                const char*             label,
                                const std::string&      stage,
                                std::array<char, 128>&  value,
                                const bool              allowEmpty = false,
                                const bool              includeCurrent = true)
        {
            bool changed = false;
            auto options = collectProjectShaderIds(ctx, stage, value.data(), includeCurrent);

            ImGui::PushID(label);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::SameLine(120.0f);
            ImGui::SetNextItemWidth(280.0f);
            const char* preview = value[0] == '\0' ? "<none>" : value.data();
            if (ImGui::BeginCombo("##shader", preview))
            {
                if (allowEmpty && ImGui::Selectable("<none>", value[0] == '\0'))
                {
                    value.fill('\0');
                    changed = true;
                }
                for (const auto& option : options)
                {
                    const bool selected = option == value.data();
                    if (ImGui::Selectable(option.c_str(), selected))
                    {
                        copyName(value, option);
                        changed = true;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            ImGui::SameLine();
            ImGui::SetNextItemWidth(220.0f);
            changed |= ImGui::InputText("##manual", value.data(), value.size());
            ImGui::PopID();
            return changed;
        }

        bool drawShaderLibrarySelector(const char* label, std::array<char, 128>& value)
        {
            bool changed = false;
            std::array options {"project", "builtin"};

            ImGui::PushID(label);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::SameLine(120.0f);
            ImGui::SetNextItemWidth(160.0f);
            const char* preview = value[0] == '\0' ? "project" : value.data();
            if (ImGui::BeginCombo("##library", preview))
            {
                for (const char* option : options)
                {
                    const bool selected = std::string_view(preview) == option;
                    if (ImGui::Selectable(option, selected))
                    {
                        copyName(value, option);
                        changed = true;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            ImGui::SameLine();
            ImGui::SetNextItemWidth(160.0f);
            changed |= ImGui::InputText("##manual", value.data(), value.size());
            ImGui::PopID();
            return changed;
        }

        bool drawBuiltinFullscreenVertexField(std::array<char, 128>& vertex)
        {
            if (std::string_view(vertex.data()) != "fullscreen_triangle.vert")
            {
                copyName(vertex, "fullscreen_triangle.vert");
                return true;
            }

            ImGui::PushID("BuiltinFullscreenVertex");
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Vertex");
            ImGui::SameLine(120.0f);
            ImGui::SetNextItemWidth(280.0f);
            ImGui::BeginDisabled();
            ImGui::InputText("##builtinVertex", vertex.data(), vertex.size());
            ImGui::EndDisabled();
            ImGui::PopID();
            return false;
        }

        bool normalizeGraphicsShaderSelection(EditorContext& ctx, RenderGraphPassEditState& state)
        {
            bool changed = false;

            if (state.vertexLibrary[0] == '\0')
                copyName(state.vertexLibrary, "builtin");
            if (state.fragmentLibrary[0] == '\0')
                copyName(state.fragmentLibrary, "project");

            if (std::string_view(state.vertexLibrary.data()) == "builtin")
            {
                if (std::string_view(state.vertex.data()) != "fullscreen_triangle.vert")
                {
                    copyName(state.vertex, "fullscreen_triangle.vert");
                    changed = true;
                }
            }
            else if (std::string_view(state.vertexLibrary.data()) == "project")
            {
                const auto projectVertices = collectProjectShaderIds(ctx, "vert", "", false);
                if (projectVertices.empty())
                {
                    copyName(state.vertexLibrary, "builtin");
                    copyName(state.vertex, "fullscreen_triangle.vert");
                    changed = true;
                }
                else if (std::find(projectVertices.begin(), projectVertices.end(), state.vertex.data()) ==
                         projectVertices.end())
                {
                    copyName(state.vertex, projectVertices.front());
                    changed = true;
                }
            }

            const auto projectFragments = collectProjectShaderIds(ctx, "frag", "", false);
            const auto builtinFragments = collectBuiltinShaderIds("frag");
            if (std::string_view(state.fragmentLibrary.data()) == "builtin")
            {
                if (std::find(builtinFragments.begin(), builtinFragments.end(), state.fragment.data()) ==
                    builtinFragments.end())
                {
                    if (std::find(projectFragments.begin(), projectFragments.end(), state.fragment.data()) !=
                        projectFragments.end())
                    {
                        copyName(state.fragmentLibrary, "project");
                    }
                    else if (!builtinFragments.empty())
                    {
                        copyName(state.fragment, builtinFragments.front());
                    }
                    else if (!projectFragments.empty())
                    {
                        copyName(state.fragmentLibrary, "project");
                        copyName(state.fragment, projectFragments.front());
                    }
                    changed = true;
                }
            }
            else if (std::string_view(state.fragmentLibrary.data()) == "project")
            {
                if (std::find(projectFragments.begin(), projectFragments.end(), state.fragment.data()) ==
                    projectFragments.end())
                {
                    if (!projectFragments.empty())
                    {
                        copyName(state.fragment, projectFragments.front());
                    }
                    else if (!builtinFragments.empty())
                    {
                        copyName(state.fragmentLibrary, "builtin");
                        copyName(state.fragment, builtinFragments.front());
                    }
                    changed = true;
                }
            }

            return changed;
        }

        bool normalizePipelineShaderSelection(EditorContext& ctx, RenderGraphPassEditState& state)
        {
            bool changed = false;
            if (state.pipeline == 1)
            {
                changed |= normalizeShaderForLibrary(ctx, state.library, "comp", state.compute);
            }
            else if (state.pipeline == 2)
            {
                changed |= normalizeShaderForLibrary(ctx, state.library, "rgen", state.raygen);
                changed |= normalizeShaderForLibrary(ctx, state.library, "rmiss", state.miss, true);
                changed |= normalizeShaderForLibrary(ctx, state.library, "rchit", state.closestHit, true);
                changed |= normalizeShaderForLibrary(ctx, state.library, "rahit", state.anyHit, true);
            }
            else
            {
                changed |= normalizeGraphicsShaderSelection(ctx, state);
            }
            return changed;
        }

        std::filesystem::path scriptDialogStartPath(EditorContext& ctx, const std::string& uri)
        {
            const auto assetRoot = editorAssetRoot(ctx);
            if (uri.rfind("res://", 0) == 0)
            {
                const auto resolved = (assetRoot / uri.substr(6)).lexically_normal();
                if (resolved.has_parent_path())
                    return resolved.parent_path();
            }
            return assetRoot.empty() ? std::filesystem::path {"."} : assetRoot;
        }

        std::string assetUuidToUri(EditorContext* ctx, const vultra::CoreUUID& uuid)
        {
            if (!ctx || !ctx->services || !uuid.valid())
                return {};

            auto* assetService = ctx->services->tryGet<vultra::IAssetService>();
            if (!assetService)
                return {};

            std::string uri;
            if (!assetService->resolver().resolve(uuid.native(), uri))
                return {};
            return uri;
        }

        bool isImportedAssetPath(const std::string& path)
        {
            if (path.empty())
                return false;
            std::filesystem::path fsPath {path};
            return !fsPath.empty() && *fsPath.begin() == "imported";
        }

        bool isUserSelectableAssetEntry(const vasset::VAssetRegistry::AssetEntry& entry)
        {
            return !entry.sourcePath.empty() && !isImportedAssetPath(entry.sourcePath);
        }

        std::string entrySourceUri(const vasset::VAssetRegistry::AssetEntry& entry)
        {
            if (entry.sourcePath.empty())
                return {};
            return "res://" + std::filesystem::path(entry.sourcePath).generic_string();
        }

        std::string assetDisplayName(const vasset::VAssetRegistry::AssetEntry& entry)
        {
            const auto source = std::filesystem::path(entry.sourcePath);
            auto       name   = source.stem().generic_string();
            if (name.empty())
                name = source.filename().generic_string();
            if (name.empty())
                name = entry.importedPath;
            return name;
        }

        bool tryParseUuidString(const std::string& text, vultra::CoreUUID& out)
        {
            vbase::UUID parsed {};
            if (!vbase::try_parse_uuid(text.c_str(), parsed))
                return false;
            out = vultra::CoreUUID(parsed);
            return out.valid();
        }

        bool drawAssetRegistryPicker(EditorContext*     ctx,
                                     const char*        popupId,
                                     vasset::VAssetType expectedType,
                                     vultra::CoreUUID&  uuid)
        {
            if (!ctx || !ctx->services || expectedType == vasset::VAssetType::eUnknown)
                return false;

            auto* assetService = ctx->services->tryGet<vultra::IAssetService>();
            if (!assetService)
                return false;

            bool changed = false;
            if (ImGui::BeginPopup(popupId))
            {
                ImGui::Text("Select %s", vasset::toString(expectedType).c_str());
                ImGui::Separator();
                bool any = false;
                for (const auto& [uuidText, entry] : assetService->registry().getRegistry())
                {
                    if (entry.type != expectedType)
                        continue;
                    if (!isUserSelectableAssetEntry(entry))
                        continue;

                    vultra::CoreUUID candidate;
                    if (!tryParseUuidString(uuidText, candidate))
                        continue;

                    any              = true;
                    const auto label = assetDisplayName(entry) + "##" + uuidText;
                    if (ImGui::Selectable(label.c_str(), uuid == candidate))
                    {
                        uuid    = candidate;
                        changed = true;
                        ImGui::CloseCurrentPopup();
                    }
                    const auto sourceUri = entrySourceUri(entry);
                    if (!sourceUri.empty())
                    {
                        ImGui::SameLine();
                        ImGui::TextDisabled("%s", sourceUri.c_str());
                    }
                }
                if (!any)
                    ImGui::TextDisabled("No source assets of this type.");
                ImGui::EndPopup();
            }
            return changed;
        }

        bool drawUuidObjectField(EditorContext* ctx, vultra::CoreUUID& uuid, const char* fieldName, const char* label)
        {
            bool       changed = false;
            const auto uri     = assetUuidToUri(ctx, uuid);
            const auto text =
                uuid.valid() ? (!uri.empty() ? uri : uuid.toString()) :
                               std::string(ICON_MDI_BULLSEYE "  None (") + expectedAssetLabelForField(fieldName) + ")";
            const auto dialogKey = std::string("InspectorSelectAsset_") + fieldName;

            ImGui::TextUnformatted(label);
            ImGui::PushID(label);
            const float buttonSize = ImGui::GetFrameHeight();
            const float spacing    = ImGui::GetStyle().ItemSpacing.x;
            const float fieldWidth =
                std::max(1.0f, ImGui::GetContentRegionAvail().x - buttonSize * 2.0f - spacing * 2.0f);
            ImGui::Button(text.c_str(), ImVec2(fieldWidth, 0.0f));
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Drop a %s asset here.", expectedAssetLabelForField(fieldName));

            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetUuidPayload))
                {
                    if (payload->DataSize == sizeof(vultra::CoreUUID))
                    {
                        vultra::CoreUUID dropped;
                        std::memcpy(&dropped, payload->Data, sizeof(dropped));
                        if (isAcceptedAssetUuid(ctx, dropped, fieldName))
                        {
                            uuid    = dropped;
                            changed = true;
                        }
                        else if (ctx)
                        {
                            ctx->state.statusMessage = "Dropped asset type is not accepted by this field.";
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_MDI_BULLSEYE) && ctx)
                ImGui::OpenPopup(dialogKey.c_str());
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Select %s asset.", expectedAssetLabelForField(fieldName));

            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_MDI_CLOSE_CIRCLE_OUTLINE) && uuid.valid())
            {
                uuid    = {};
                changed = true;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Clear reference.");

            changed |= drawAssetRegistryPicker(ctx, dialogKey.c_str(), expectedAssetTypeForField(fieldName), uuid);
            ImGui::PopID();

            return changed;
        }

        bool drawAnimatorComponentFields(EditorContext& ctx, vultra::AnimatorComponent& animator)
        {
            bool changed = false;

            changed |= drawUuidObjectField(&ctx, animator.skeleton, "skeleton", "Skeleton");
            changed |= drawUuidObjectField(&ctx, animator.animation, "animation", "Animation");

            ImGui::Separator();
            changed |= ImGui::Checkbox("Play On Start", &animator.playOnStart);
            changed |= ImGui::Checkbox("Playing", &animator.playing);
            changed |= ImGui::Checkbox("Loop", &animator.loop);
            changed |= ImGui::DragFloat("Speed", &animator.speed, 0.01f, -8.0f, 8.0f, "%.3f");

            float time = std::max(animator.time, 0.0f);
            if (ImGui::DragFloat("Time", &time, 0.01f, 0.0f, 0.0f, "%.3f s"))
            {
                animator.time = std::max(time, 0.0f);
                changed       = true;
            }

            if (ImGui::Button(ICON_MDI_RESTART "  Reset Time", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
            {
                animator.time = 0.0f;
                changed       = true;
            }
            return changed;
        }

        bool drawScriptUriObjectField(EditorContext* ctx, std::string& uri, const char* label)
        {
            bool       changed = false;
            const auto text    = uri.empty() ? std::string(ICON_MDI_BULLSEYE "  None (Lua Script)") :
                                               std::string(ICON_MDI_LANGUAGE_LUA "  ") + uri;

            ImGui::TextUnformatted(label);
            ImGui::PushID(label);
            const float buttonSize = ImGui::GetFrameHeight();
            const float fieldWidth =
                std::max(1.0f, ImGui::GetContentRegionAvail().x - buttonSize - ImGui::GetStyle().ItemSpacing.x);
            if (ImGui::Button(text.c_str(), ImVec2(fieldWidth, 0.0f)) && ctx)
            {
                IGFD::FileDialogConfig config;
                config.path  = scriptDialogStartPath(*ctx, uri).generic_string();
                config.flags = ImGuiFileDialogFlags_Modal | ImGuiFileDialogFlags_HideColumnType |
                               ImGuiFileDialogFlags_HideColumnSize | ImGuiFileDialogFlags_HideColumnDate |
                               ImGuiFileDialogFlags_DontShowHiddenFiles |
                               ImGuiFileDialogFlags_CaseInsensitiveExtentionFiltering |
                               ImGuiFileDialogFlags_NaturalSorting | ImGuiFileDialogFlags_DisableThumbnailMode;
                ImGuiFileDialog::Instance()->OpenDialog(kScriptDialogKey, "Select Lua Script", ".lua", config);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Click to choose a .lua script, or drop a Lua script asset here.");

            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetUuidPayload))
                {
                    if (payload->DataSize == sizeof(vultra::CoreUUID))
                    {
                        vultra::CoreUUID dropped;
                        std::memcpy(&dropped, payload->Data, sizeof(dropped));
                        if (ctx && ctx->services)
                        {
                            auto*      assetService = ctx->services->tryGet<vultra::IAssetService>();
                            const auto entry        = assetService ? assetService->registry().lookup(dropped.native()) :
                                                                     vasset::VAssetRegistry::AssetEntry {};
                            if (entry.type == vasset::VAssetType::eScriptLua)
                            {
                                const auto resolvedUri = assetUuidToUri(ctx, dropped);
                                if (!resolvedUri.empty())
                                {
                                    uri     = resolvedUri;
                                    changed = true;
                                }
                            }
                            else
                            {
                                ctx->state.statusMessage = "Dropped asset is not a Lua script.";
                            }
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_MDI_BULLSEYE))
            {
                if (ctx)
                {
                    IGFD::FileDialogConfig config;
                    config.path  = scriptDialogStartPath(*ctx, uri).generic_string();
                    config.flags = ImGuiFileDialogFlags_Modal | ImGuiFileDialogFlags_HideColumnType |
                                   ImGuiFileDialogFlags_HideColumnSize | ImGuiFileDialogFlags_HideColumnDate |
                                   ImGuiFileDialogFlags_DontShowHiddenFiles |
                                   ImGuiFileDialogFlags_CaseInsensitiveExtentionFiltering |
                                   ImGuiFileDialogFlags_NaturalSorting | ImGuiFileDialogFlags_DisableThumbnailMode;
                    ImGuiFileDialog::Instance()->OpenDialog(kScriptDialogKey, "Select Lua Script", ".lua", config);
                }
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Select Lua script.");
            ImGui::PopID();

            ui::ScopedPopupStyle fileDialogStyle;
            if (ctx &&
                ImGuiFileDialog::Instance()->Display(kScriptDialogKey,
                                                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings,
                                                     ImVec2(640.0f, 420.0f)))
            {
                if (ImGuiFileDialog::Instance()->IsOk())
                {
                    const auto selected = std::filesystem::path(ImGuiFileDialog::Instance()->GetFilePathName(
                                                                    IGFD_ResultMode_KeepInputFile))
                                              .lexically_normal();
                    if (selected.extension() == ".lua")
                    {
                        const auto selectedUri = pathToResUri(*ctx, selected);
                        if (!selectedUri.empty())
                        {
                            uri     = selectedUri;
                            changed = true;
                        }
                        else
                        {
                            ctx->state.statusMessage = "Script must be inside the project asset root.";
                        }
                    }
                }
                ImGuiFileDialog::Instance()->Close();
            }

            if (!uri.empty() && ImGui::SmallButton((std::string(ICON_MDI_CLOSE "  Clear##") + label).c_str()))
            {
                uri.clear();
                changed = true;
            }
            return changed;
        }

        std::vector<std::string> collectMaterialGraphUris(EditorContext& ctx)
        {
            std::vector<std::string> out;
            const auto               root = editorAssetRoot(ctx);
            std::error_code ec;
            if (root.empty() || !std::filesystem::exists(root, ec))
                return out;
            for (auto it = std::filesystem::recursive_directory_iterator(
                     root, std::filesystem::directory_options::skip_permission_denied, ec);
                 it != std::filesystem::recursive_directory_iterator {};
                 it.increment(ec))
            {
                if (ec)
                {
                    ec.clear();
                    continue;
                }
                const auto& entry = *it;
                if (!entry.is_regular_file(ec))
                {
                    ec.clear();
                    continue;
                }
                const auto path = entry.path();
                const auto name = path.filename().generic_string();
                const auto ext  = path.extension().generic_string();
                if (ext != ".vmatgraph" && name.find(".vmatgraph.json") == std::string::npos)
                    continue;
                const auto      rel = std::filesystem::relative(path, root, ec);
                if (!ec && !rel.empty() && isImportedAssetPath(rel.generic_string()))
                    continue;
                if (auto uri = pathToResUri(ctx, path); !uri.empty())
                    out.push_back(std::move(uri));
            }
            std::sort(out.begin(), out.end());
            return out;
        }

        bool drawMaterialGraphUriField(EditorContext* ctx, std::string& uri, const char* label)
        {
            bool changed = false;
            ImGui::TextUnformatted(label);
            ImGui::PushID(label);
            const auto preview = uri.empty() ? "<none>" : uri.c_str();
            ImGui::SetNextItemWidth(std::max(
                1.0f, ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x));
            if (ImGui::BeginCombo("##MaterialGraphUri", preview))
            {
                if (ImGui::Selectable("<none>", uri.empty()))
                {
                    uri.clear();
                    changed = true;
                }
                if (ctx)
                {
                    for (const auto& candidate : collectMaterialGraphUris(*ctx))
                    {
                        if (ImGui::Selectable(candidate.c_str(), candidate == uri))
                        {
                            uri     = candidate;
                            changed = true;
                        }
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_MDI_CLOSE) && !uri.empty())
            {
                uri.clear();
                changed = true;
            }
            ImGui::PopID();
            return changed;
        }

        bool drawRendererKeyCombo(EditorContext* ctx, std::string& rendererKey, const char* label)
        {
            auto* renderService = ctx && ctx->services ? ctx->services->tryGet<vultra::IRenderService>() : nullptr;
            auto* renderBackend =
                ctx && ctx->services ? ctx->services->tryGet<vultra::IRenderBackendService>() : nullptr;
            auto keys = renderService ? renderService->rendererKeys() : std::vector<std::string> {};

            keys.erase(std::remove(keys.begin(), keys.end(), "editor-shell"), keys.end());

            if (rendererKey.empty())
                rendererKey = "universal";

            if (std::find(keys.begin(), keys.end(), rendererKey) == keys.end())
                keys.push_back(rendererKey);

            std::sort(keys.begin(), keys.end());
            keys.erase(std::unique(keys.begin(), keys.end()), keys.end());

            bool        changed = false;
            const char* preview = rendererKey.empty() ? "<none>" : rendererKey.c_str();
            if (ImGui::BeginCombo(label, preview))
            {
                const bool rayTracingAvailable =
                    renderBackend && HasFlagValues(renderBackend->renderDevice().getFeatureFlag(),
                                                   vultra::rhi::RenderDeviceFeatureFlagBits::eRayTracingPipeline);
                for (const auto& key : keys)
                {
                    const bool selected = key == rendererKey;
                    const bool disabled = (key == "universal_rt" || key == "default_rt") && !rayTracingAvailable;
                    if (disabled)
                        ImGui::BeginDisabled();
                    if (ImGui::Selectable(key.c_str(), selected))
                    {
                        rendererKey = key;
                        changed     = true;
                    }
                    if (disabled)
                    {
                        ImGui::EndDisabled();
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                            ImGui::SetTooltip("Ray tracing is not available on the current render device.");
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            return changed;
        }

        bool drawMetaValue(EditorContext*            ctx,
                           ui::TextureSelectorState* textureSelector,
                           ui::MeshSelectorState*    meshSelector,
                           const entt::meta_data&    field,
                           entt::meta_any&           value,
                           const char*               fieldName,
                           const char*               label)
        {
            bool changed = false;
            fieldName    = fieldName ? fieldName : "";

            if (auto* v = value.try_cast<bool>())
                return ImGui::Checkbox(label, v);

            if (auto* v = value.try_cast<float>())
                return ImGui::DragFloat(label, v, 0.05f);

            if (auto* v = value.try_cast<int>())
                return ImGui::InputInt(label, v);

            if (auto* v = value.try_cast<uint32_t>())
            {
                if (std::strcmp(fieldName, "projection") == 0)
                {
                    const char* projectionLabels[] = {"Perspective", "Orthographic"};
                    int         projectionIndex    = static_cast<int>(std::min(*v, 1u));
                    if (ImGui::Combo(label, &projectionIndex, projectionLabels, IM_ARRAYSIZE(projectionLabels)))
                    {
                        *v      = static_cast<uint32_t>(projectionIndex);
                        changed = true;
                    }
                    return changed;
                }

                if (std::strcmp(fieldName, "clearMode") == 0)
                {
                    const char* clearModeLabels[] = {"Color", "Skybox"};
                    int         clearModeIndex    = static_cast<int>(std::min(*v, 1u));
                    if (ImGui::Combo(label, &clearModeIndex, clearModeLabels, IM_ARRAYSIZE(clearModeLabels)))
                    {
                        *v      = static_cast<uint32_t>(clearModeIndex);
                        changed = true;
                    }
                    return changed;
                }

                if (std::strcmp(fieldName, "kind") == 0)
                {
                    const char* lightKindLabels[] = {"Directional", "Point", "Spot", "Rectangle Area"};
                    int         kindIndex         = static_cast<int>(std::min(*v, 3u));
                    if (ImGui::Combo(label, &kindIndex, lightKindLabels, IM_ARRAYSIZE(lightKindLabels)))
                    {
                        *v      = static_cast<uint32_t>(std::clamp(kindIndex, 0, IM_ARRAYSIZE(lightKindLabels) - 1));
                        changed = true;
                    }
                    return changed;
                }

                if (std::strcmp(fieldName, "motionType") == 0)
                {
                    const char* labels[] = {"Static", "Kinematic", "Dynamic"};
                    int         index    = static_cast<int>(std::min(*v, 2u));
                    if (ImGui::Combo(label, &index, labels, IM_ARRAYSIZE(labels)))
                    {
                        *v      = static_cast<uint32_t>(std::clamp(index, 0, IM_ARRAYSIZE(labels) - 1));
                        changed = true;
                    }
                    return changed;
                }

                if (std::strcmp(fieldName, "objectLayer") == 0)
                {
                    const char* labels[] = {"Non Moving", "Moving"};
                    int         index    = static_cast<int>(std::min(*v, 1u));
                    if (ImGui::Combo(label, &index, labels, IM_ARRAYSIZE(labels)))
                    {
                        *v      = static_cast<uint32_t>(index);
                        changed = true;
                    }
                    return changed;
                }

                if (std::strcmp(fieldName, "motionQuality") == 0)
                {
                    const char* labels[] = {"Discrete", "Linear Cast"};
                    int         index    = static_cast<int>(std::min(*v, 1u));
                    if (ImGui::Combo(label, &index, labels, IM_ARRAYSIZE(labels)))
                    {
                        *v      = static_cast<uint32_t>(index);
                        changed = true;
                    }
                    return changed;
                }

                if (std::strcmp(fieldName, "builtinGeometry") == 0)
                {
                    const char* geometryLabels[] = {"External Mesh", "Quad", "Cube", "Sphere", "Capsule"};
                    int         geometryIndex    = *v == UINT32_MAX ? 0 : static_cast<int>(std::min(*v + 1u, 4u));
                    if (ImGui::Combo(label, &geometryIndex, geometryLabels, IM_ARRAYSIZE(geometryLabels)))
                    {
                        *v      = geometryIndex == 0 ? UINT32_MAX : static_cast<uint32_t>(geometryIndex - 1);
                        changed = true;
                    }
                    return changed;
                }

                if (std::strcmp(fieldName, "shape") == 0)
                {
                    const char* shapeLabels[] = {"Box", "Sphere"};
                    int         shapeIndex    = static_cast<int>(std::min(*v, 1u));
                    if (ImGui::Combo(label, &shapeIndex, shapeLabels, IM_ARRAYSIZE(shapeLabels)))
                    {
                        *v      = static_cast<uint32_t>(shapeIndex);
                        changed = true;
                    }
                    return changed;
                }

                int temp = static_cast<int>(*v);
                if (ImGui::InputInt(label, &temp))
                {
                    *v      = static_cast<uint32_t>(std::max(temp, 0));
                    changed = true;
                }
                return changed;
            }

            if (auto* v = value.try_cast<std::string>())
            {
                if (std::strcmp(fieldName, "scriptUri") == 0)
                    return drawScriptUriObjectField(ctx, *v, label);
                if (std::strcmp(fieldName, "rendererKey") == 0)
                    return drawRendererKeyCombo(ctx, *v, label);

                std::array<char, 256> buffer {};
                copyName(buffer, *v);
                if (ImGui::InputText(label, buffer.data(), buffer.size()))
                {
                    *v      = buffer.data();
                    changed = true;
                }
                return changed;
            }

            if (auto* v = value.try_cast<glm::vec3>())
                return ImGui::DragFloat3(label, &v->x, 0.05f);

            if (auto* v = value.try_cast<glm::vec4>())
                return ImGui::ColorEdit4(label, &v->x);

            if (auto* v = value.try_cast<glm::quat>())
            {
                glm::vec3 eulerDeg = glm::degrees(glm::eulerAngles(*v));
                if (ImGui::DragFloat3(label, &eulerDeg.x, 0.5f))
                {
                    *v      = glm::quat(glm::radians(eulerDeg));
                    changed = true;
                }
                return changed;
            }

            if (auto* v = value.try_cast<vultra::CoreUUID>())
            {
                if (ctx && textureSelector && expectedAssetTypeForField(fieldName) == vasset::VAssetType::eTexture)
                    return ui::drawTextureUuidField(*ctx, label, *v, *textureSelector);
                if (ctx && meshSelector && expectedAssetTypeForField(fieldName) == vasset::VAssetType::eMesh)
                    return ui::drawMeshUuidField(*ctx, label, *v, *meshSelector);
                return drawUuidObjectField(ctx, *v, fieldName, label);
            }

            const auto typeName = field.type() ? field.type().name() : "<unknown>";
            ImGui::TextDisabled("%s: <%s>", label, typeName ? typeName : "unregistered");
            return false;
        }

        template<typename Component>
        bool shouldDrawMetaField(const Component&, const char*)
        {
            return true;
        }

        template<>
        bool shouldDrawMetaField(const vultra::CameraComponent& camera, const char* fieldName)
        {
            if (std::strcmp(fieldName, "clearColor") == 0)
                return camera.clearMode == 0u;
            return true;
        }

        template<>
        bool shouldDrawMetaField(const vultra::MeshComponent& mesh, const char* fieldName)
        {
            if (std::strcmp(fieldName, "mesh") == 0)
                return mesh.builtinGeometry == UINT32_MAX;
            return std::strcmp(fieldName, "materialOverrides") != 0;
        }

        template<>
        bool shouldDrawMetaField(const vultra::EnvironmentComponent& environment, const char* fieldName)
        {
            if (std::strcmp(fieldName, "iblColor") == 0 || std::strcmp(fieldName, "iblIntensity") == 0)
                return environment.enableIBL;
            return true;
        }

        template<>
        bool shouldDrawMetaField(const vultra::ReflectionProbeComponent& probe, const char* fieldName)
        {
            if (std::strcmp(fieldName, "environmentMap") == 0 || std::strcmp(fieldName, "intensity") == 0 ||
                std::strcmp(fieldName, "parallaxCorrection") == 0)
                return probe.enableIBL;
            if (std::strcmp(fieldName, "boxSize") == 0)
                return probe.shape == 0u;
            if (std::strcmp(fieldName, "radius") == 0)
                return probe.shape == 1u;
            return true;
        }

        template<>
        bool shouldDrawMetaField(const vultra::RigidBodyComponent& body, const char* fieldName)
        {
            if (std::strcmp(fieldName, "linearVelocity") == 0 || std::strcmp(fieldName, "angularVelocity") == 0)
                return body.motionType != 0u;
            if (std::strcmp(fieldName, "mass") == 0)
                return body.overrideMass;
            if (std::strcmp(fieldName, "maxLinearVelocity") == 0 || std::strcmp(fieldName, "maxAngularVelocity") == 0)
                return body.motionType == 2u;
            return true;
        }

        struct MaterialSlotChoice
        {
            uint32_t    slot {0};
            std::string label;
            std::string detail;
        };

        std::vector<MaterialSlotChoice> collectMaterialSlotChoices(EditorContext*               ctx,
                                                                   const vultra::MeshComponent& mesh)
        {
            std::vector<MaterialSlotChoice> choices;

            if (!ctx || !ctx->services || !mesh.mesh.valid())
            {
                choices.push_back({.slot = 0u, .label = "Slot 0 - Default Material"});
                return choices;
            }

            auto* assets = ctx->services->tryGet<vultra::IAssetService>();
            if (!assets)
            {
                choices.push_back({.slot = 0u, .label = "Slot 0 - Default Material"});
                return choices;
            }

            const auto  handle  = assets->loadMeshAsync(mesh.mesh);
            const auto* cpuMesh = handle.cpu();
            if (!cpuMesh)
            {
                choices.push_back({.slot = 0u, .label = "Slot 0 - Loading Mesh Materials"});
                return choices;
            }

            uint32_t slotCount = static_cast<uint32_t>(cpuMesh->materials.size());
            for (const auto& subMesh : cpuMesh->subMeshes)
                slotCount = std::max(slotCount, subMesh.materialIndex + 1u);
            slotCount = std::max(slotCount, 1u);

            choices.reserve(slotCount);
            for (uint32_t slot = 0; slot < slotCount; ++slot)
            {
                std::string materialName;
                if (slot < cpuMesh->materials.size())
                    materialName = cpuMesh->materials[slot].name;
                if (materialName.empty())
                    materialName = "Material " + std::to_string(slot);

                std::string subMeshName;
                for (const auto& subMesh : cpuMesh->subMeshes)
                {
                    if (subMesh.materialIndex == slot && !subMesh.name.empty())
                    {
                        subMeshName = subMesh.name;
                        break;
                    }
                }

                MaterialSlotChoice choice;
                choice.slot   = slot;
                choice.label  = "Slot " + std::to_string(slot) + " - " + materialName;
                choice.detail = subMeshName.empty() ? std::string {} : "Used by submesh: " + subMeshName;
                choices.push_back(std::move(choice));
            }
            return choices;
        }

        bool drawMaterialSlotCombo(EditorContext* ctx, const vultra::MeshComponent& mesh, uint32_t& slot)
        {
            auto       choices    = collectMaterialSlotChoices(ctx, mesh);
            const auto selectedIt = std::find_if(
                choices.begin(), choices.end(), [&](const MaterialSlotChoice& choice) { return choice.slot == slot; });
            std::string selectedLabel = selectedIt != choices.end() ?
                                            selectedIt->label :
                                            "Slot " + std::to_string(slot) + " - Unknown Material";

            bool changed = false;
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("Slot", selectedLabel.c_str()))
            {
                for (const auto& choice : choices)
                {
                    const bool selected = choice.slot == slot;
                    if (ImGui::Selectable(choice.label.c_str(), selected))
                    {
                        slot    = choice.slot;
                        changed = true;
                    }
                    if (!choice.detail.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                        ImGui::SetTooltip("%s", choice.detail.c_str());
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                if (selectedIt == choices.end())
                {
                    ImGui::Separator();
                    const std::string unknown = "Keep Slot " + std::to_string(slot);
                    if (ImGui::Selectable(unknown.c_str(), true))
                        changed = false;
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Material graph overrides replace one mesh material slot. The saved value is still "
                                  "the numeric slot.");
            return changed;
        }

        template<typename Component>
        bool drawMetaFields(EditorContext*                          ctx,
                            ui::TextureSelectorState*               textureSelector,
                            Component&                              component,
                            const std::function<void(const char*)>& onChanged    = {},
                            ui::MeshSelectorState*                  meshSelector = nullptr)
        {
            bool changedAny = false;
            auto instance   = entt::forward_as_meta(component);
            auto meta       = entt::resolve<Component>();
            ImGui::PushID(static_cast<int>(meta.id()));
            for (auto [fieldId, field] : meta.data())
            {
                const char* rawName = field.name() ? field.name() : metaFieldNameFromId(fieldId);
                if (rawName == nullptr)
                    continue;
                if (!shouldDrawMetaField(component, rawName))
                    continue;

                auto value = field.get(instance);
                if (!value)
                    continue;

                const auto label = displayFieldName(rawName);
                ImGui::PushID(static_cast<int>(fieldId));
                if (drawMetaValue(ctx, textureSelector, meshSelector, field, value, rawName, label.c_str()))
                {
                    field.set(instance, value);
                    changedAny = true;
                    if (onChanged)
                        onChanged(rawName);
                }
                ImGui::PopID();
            }
            ImGui::PopID();
            return changedAny;
        }

        bool drawMeshComponentFields(EditorContext*            ctx,
                                     ui::TextureSelectorState* textureSelector,
                                     ui::MeshSelectorState*    meshSelector,
                                     vultra::MeshComponent&    mesh)
        {
            bool changed = drawMetaFields(ctx, textureSelector, mesh, {}, meshSelector);
            ImGui::Spacing();
            if (ImGui::CollapsingHeader("Material Overrides", ImGuiTreeNodeFlags_DefaultOpen))
            {
                int removeIndex = -1;
                for (int i = 0; i < static_cast<int>(mesh.materialOverrides.size()); ++i)
                {
                    auto& override = mesh.materialOverrides[static_cast<size_t>(i)];
                    ImGui::PushID(i);
                    if (drawMaterialSlotCombo(ctx, mesh, override.slot))
                        changed = true;
                    if (drawMaterialGraphUriField(ctx, override.materialGraph, "Graph"))
                        changed = true;
                    if (ImGui::SmallButton(ICON_MDI_DELETE " Remove"))
                        removeIndex = i;
                    ImGui::Separator();
                    ImGui::PopID();
                }
                if (removeIndex >= 0)
                {
                    mesh.materialOverrides.erase(mesh.materialOverrides.begin() + removeIndex);
                    changed = true;
                }
                if (ImGui::Button(ICON_MDI_PLUS " Add Material Override",
                                  ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
                {
                    mesh.materialOverrides.push_back({});
                    changed = true;
                }
            }
            return changed;
        }

        template<typename Component>
        const char* componentLabel()
        {
            const char* metaName = entt::resolve<Component>().name();
            const char* label =
                (metaName != nullptr) ? displayComponentName(metaName) : componentDisplayName<Component>();
            if (std::strcmp(label, "Component") == 0)
                label = componentDisplayName<Component>();
            return label;
        }

        template<typename Component>
        bool componentHeader(vultra::World& world, entt::entity entity)
        {
            if (!world.registry().all_of<Component>(entity))
                return false;
            const char* label = componentLabel<Component>();
            return ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);
        }

        template<typename Component>
        void drawReflectedComponent(vultra::World&                                      world,
                                    entt::entity                                        entity,
                                    EditorContext*                                      ctx,
                                    const std::function<void(Component&, const char*)>& onChanged = {})
        {
            if (!componentHeader<Component>(world, entity))
                return;

            auto& component = world.registry().get<Component>(entity);
            drawMetaFields<Component>(ctx, component, [&](const char* fieldName) {
                if (onChanged)
                    onChanged(component, fieldName);
            });
        }

        struct AddComponentDescriptor
        {
            const char* key {};
            const char* label {};
            const char* category {};
            bool (*has)(entt::registry&, entt::entity) {};
            std::function<void(EditorContext&, entt::registry&, entt::entity)> add;
        };

        template<typename Component>
        AddComponentDescriptor addComponentDescriptor(const char* key, const char* label, const char* category)
        {
            return AddComponentDescriptor {
                key,
                label,
                category,
                [](entt::registry& reg, entt::entity entity) { return reg.all_of<Component>(entity); },
                [](EditorContext&, entt::registry& reg, entt::entity entity) {
                    if (!reg.all_of<Component>(entity))
                        reg.emplace<Component>(entity);
                },
            };
        }

        void autoConfigureAnimatorFromSkinnedMesh(EditorContext& ctx, entt::registry& reg, entt::entity entity)
        {
            if (!ctx.services || !reg.all_of<vultra::AnimatorComponent>(entity))
                return;

            auto* assets = ctx.services->tryGet<vultra::IAssetService>();
            if (!assets)
                return;

            const auto findMeshEntity = [&](auto&& self, entt::entity cursor) -> entt::entity {
                if (cursor == entt::null || !reg.valid(cursor))
                    return entt::null;
                if (const auto* mesh = reg.try_get<vultra::MeshComponent>(cursor); mesh && mesh->mesh.valid())
                    return cursor;

                const auto* hierarchy = reg.try_get<vultra::HierarchyComponent>(cursor);
                if (!hierarchy)
                    return entt::null;

                for (auto child = hierarchy->firstChild; child != entt::null;)
                {
                    const auto* childHierarchy = reg.try_get<vultra::HierarchyComponent>(child);
                    const auto  next          = childHierarchy ? childHierarchy->nextSibling : entt::null;
                    if (auto found = self(self, child); found != entt::null)
                        return found;
                    child = next;
                }
                return entt::null;
            };

            const auto meshEntity = findMeshEntity(findMeshEntity, entity);
            if (meshEntity == entt::null)
                return;

            const auto* meshComponent = reg.try_get<vultra::MeshComponent>(meshEntity);
            if (!meshComponent || !meshComponent->mesh.valid())
                return;

            auto meshHandle = assets->loadMeshSync(meshComponent->mesh);
            if (!meshHandle.ready() || !meshHandle.cpu() || !meshHandle.cpu()->hasSkin)
                return;

            auto& animator = reg.get<vultra::AnimatorComponent>(entity);
            animator.skeleton = vultra::CoreUUID(meshHandle.cpu()->skeleton);

            const auto skeletonEntry = assets->registry().lookup(animator.skeleton.native());
            const auto hashPos       = skeletonEntry.sourcePath.find('#');
            const auto sourcePrefix  = hashPos == std::string::npos ? skeletonEntry.sourcePath :
                                                                skeletonEntry.sourcePath.substr(0, hashPos);
            if (!sourcePrefix.empty())
            {
                for (const auto& [uuid, entry] : assets->registry().getRegistry())
                {
                    if (entry.type != vasset::VAssetType::eAnimation)
                        continue;
                    if (!entry.sourcePath.starts_with(sourcePrefix + "#animation/"))
                        continue;
                    vbase::UUID parsed {};
                    if (vbase::try_parse_uuid(uuid.c_str(), parsed))
                        animator.animation = vultra::CoreUUID(parsed);
                    break;
                }
            }
        }

        AddComponentDescriptor addAnimatorComponentDescriptor()
        {
            return AddComponentDescriptor {
                "Animator",
                "Animator",
                "Animation",
                [](entt::registry& reg, entt::entity entity) {
                    return reg.all_of<vultra::AnimatorComponent>(entity);
                },
                [](EditorContext& ctx, entt::registry& reg, entt::entity entity) {
                    if (!reg.all_of<vultra::AnimatorComponent>(entity))
                        reg.emplace<vultra::AnimatorComponent>(entity);
                    autoConfigureAnimatorFromSkinnedMesh(ctx, reg, entity);
                },
            };
        }

        template<typename Component>
        AddComponentDescriptor addPhysicsShapeComponentDescriptor(const char* key,
                                                                 const char* label,
                                                                 void (*fit)(EditorContext&, entt::registry&, entt::entity))
        {
            return AddComponentDescriptor {
                key,
                label,
                "Physics",
                [](entt::registry& reg, entt::entity entity) { return reg.all_of<Component>(entity); },
                [fit](EditorContext& ctx, entt::registry& reg, entt::entity entity) {
                    if (!reg.all_of<Component>(entity))
                        reg.emplace<Component>(entity);
                    if (fit)
                        fit(ctx, reg, entity);
                },
            };
        }

        const std::vector<AddComponentDescriptor>& addableComponents()
        {
            static const std::vector<AddComponentDescriptor> descriptors {
                addComponentDescriptor<vultra::TransformComponent>("Transform", "Transform", "Core"),
                addComponentDescriptor<vultra::MeshComponent>("Mesh", "Mesh", "Rendering"),
                addAnimatorComponentDescriptor(),
                addComponentDescriptor<vultra::GaussianSplatComponent>(
                    "GaussianSplat", "Gaussian Splat", "Rendering"),
                addComponentDescriptor<vultra::EnvironmentComponent>("Environment", "Environment", "Lighting"),
                addComponentDescriptor<vultra::ReflectionProbeComponent>(
                    "ReflectionProbe", "Reflection Probe", "Lighting"),
                addComponentDescriptor<vultra::LightComponent>("Light", "Light", "Lighting"),
                addComponentDescriptor<vultra::RigidBodyComponent>("RigidBody", "Rigid Body", "Physics"),
                addPhysicsShapeComponentDescriptor<vultra::BoxShapeComponent>(
                    "BoxShape", "Box Shape", fitBoxShapeToMeshBounds),
                addPhysicsShapeComponentDescriptor<vultra::SphereShapeComponent>(
                    "SphereShape", "Sphere Shape", fitSphereShapeToMeshBounds),
                addPhysicsShapeComponentDescriptor<vultra::CapsuleShapeComponent>(
                    "CapsuleShape", "Capsule Shape", fitCapsuleShapeToMeshBounds),
                addComponentDescriptor<vultra::CameraComponent>("Camera", "Camera", "Camera"),
                addComponentDescriptor<vultra::XRViewComponent>("XRView", "XR View", "Camera"),
                addComponentDescriptor<vultra::ScriptComponent>("Script", "Script", "Scripting"),
            };
            return descriptors;
        }

        const std::vector<const char*>& componentDefaultOrder()
        {
            static const std::vector<const char*> order {
                "Transform",
                "Mesh",
                "Animator",
                "GaussianSplat",
                "Environment",
                "ReflectionProbe",
                "Light",
                "RigidBody",
                "BoxShape",
                "SphereShape",
                "CapsuleShape",
                "Camera",
                "XRView",
                "Script",
                "Prefab",
            };
            return order;
        }

        bool entityHasOrderedComponent(entt::registry& reg, entt::entity entity, const std::string& key)
        {
            if (key == "Transform")
                return reg.all_of<vultra::TransformComponent>(entity);
            if (key == "Mesh")
                return reg.all_of<vultra::MeshComponent>(entity);
            if (key == "Animator")
                return reg.all_of<vultra::AnimatorComponent>(entity);
            if (key == "GaussianSplat")
                return reg.all_of<vultra::GaussianSplatComponent>(entity);
            if (key == "Environment")
                return reg.all_of<vultra::EnvironmentComponent>(entity);
            if (key == "ReflectionProbe")
                return reg.all_of<vultra::ReflectionProbeComponent>(entity);
            if (key == "Light")
                return reg.all_of<vultra::LightComponent>(entity);
            if (key == "RigidBody")
                return reg.all_of<vultra::RigidBodyComponent>(entity);
            if (key == "BoxShape")
                return reg.all_of<vultra::BoxShapeComponent>(entity);
            if (key == "SphereShape")
                return reg.all_of<vultra::SphereShapeComponent>(entity);
            if (key == "CapsuleShape")
                return reg.all_of<vultra::CapsuleShapeComponent>(entity);
            if (key == "Camera")
                return reg.all_of<vultra::CameraComponent>(entity);
            if (key == "XRView")
                return reg.all_of<vultra::XRViewComponent>(entity);
            if (key == "Script")
                return reg.all_of<vultra::ScriptComponent>(entity);
            if (key == "Prefab")
                return reg.all_of<vultra::PrefabInstanceComponent>(entity);
            return false;
        }

        void syncComponentOrder(entt::registry& reg, entt::entity entity, std::vector<std::string>& order)
        {
            order.erase(
                std::remove_if(order.begin(),
                               order.end(),
                               [&](const std::string& key) { return !entityHasOrderedComponent(reg, entity, key); }),
                order.end());

            for (const char* key : componentDefaultOrder())
            {
                if (!entityHasOrderedComponent(reg, entity, key))
                    continue;
                if (std::find(order.begin(), order.end(), key) == order.end())
                    order.emplace_back(key);
            }
        }

        const char* orderedComponentLabel(const std::string& key)
        {
            if (key == "Transform")
                return "Transform";
            if (key == "Mesh")
                return "Mesh";
            if (key == "Animator")
                return "Animator";
            if (key == "GaussianSplat")
                return "Gaussian Splat";
            if (key == "Environment")
                return "Environment";
            if (key == "ReflectionProbe")
                return "Reflection Probe";
            if (key == "Light")
                return "Light";
            if (key == "RigidBody")
                return "Rigid Body";
            if (key == "BoxShape")
                return "Box Shape";
            if (key == "SphereShape")
                return "Sphere Shape";
            if (key == "CapsuleShape")
                return "Capsule Shape";
            if (key == "Camera")
                return "Camera";
            if (key == "XRView")
                return "XR View";
            if (key == "Script")
                return "Script";
            if (key == "Prefab")
                return "Prefab";
            return "Component";
        }

        void removeOrderedComponent(entt::registry& reg, entt::entity entity, const std::string& key)
        {
            if (key == "Transform")
                reg.remove<vultra::TransformComponent>(entity);
            else if (key == "Mesh")
                reg.remove<vultra::MeshComponent>(entity);
            else if (key == "Animator")
                reg.remove<vultra::AnimatorComponent>(entity);
            else if (key == "GaussianSplat")
                reg.remove<vultra::GaussianSplatComponent>(entity);
            else if (key == "Environment")
                reg.remove<vultra::EnvironmentComponent>(entity);
            else if (key == "ReflectionProbe")
                reg.remove<vultra::ReflectionProbeComponent>(entity);
            else if (key == "Light")
                reg.remove<vultra::LightComponent>(entity);
            else if (key == "RigidBody")
                reg.remove<vultra::RigidBodyComponent>(entity);
            else if (key == "BoxShape")
                reg.remove<vultra::BoxShapeComponent>(entity);
            else if (key == "SphereShape")
                reg.remove<vultra::SphereShapeComponent>(entity);
            else if (key == "CapsuleShape")
                reg.remove<vultra::CapsuleShapeComponent>(entity);
            else if (key == "Camera")
            {
                reg.remove<vultra::CameraComponent>(entity);
                if (reg.all_of<vultra::XRViewComponent>(entity))
                    reg.remove<vultra::XRViewComponent>(entity);
            }
            else if (key == "XRView")
                reg.remove<vultra::XRViewComponent>(entity);
            else if (key == "Script")
                reg.remove<vultra::ScriptComponent>(entity);
        }

        bool orderedComponentHeader(const std::string& key,
                                    const std::size_t  index,
                                    const std::size_t  count,
                                    bool&              removeRequested,
                                    bool&              moveUpRequested,
                                    bool&              moveDownRequested)
        {
            ImGui::PushID(key.c_str());

            ImGui::SetNextItemAllowOverlap();
            const bool open = ImGui::CollapsingHeader(orderedComponentLabel(key),
                                                      ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

            const ImGuiStyle& style     = ImGui::GetStyle();
            const float       upWidth   = ImGui::CalcTextSize(ICON_MDI_ARROW_UP).x + style.FramePadding.x * 2.0f;
            const float       downWidth = ImGui::CalcTextSize(ICON_MDI_ARROW_DOWN).x + style.FramePadding.x * 2.0f;
            const float deleteWidth     = ImGui::CalcTextSize(ICON_MDI_DELETE_OUTLINE).x + style.FramePadding.x * 2.0f;
            const float buttonWidth     = upWidth + downWidth + deleteWidth + style.ItemSpacing.x * 2.0f;
            const float rightX          = ImGui::GetWindowContentRegionMax().x - buttonWidth;
            ImGui::SameLine(std::max(ImGui::GetCursorPosX(), rightX));

            ImGui::BeginDisabled(index == 0);
            if (ImGui::SmallButton(ICON_MDI_ARROW_UP))
                moveUpRequested = true;
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(index + 1 >= count);
            if (ImGui::SmallButton(ICON_MDI_ARROW_DOWN))
                moveDownRequested = true;
            ImGui::EndDisabled();
            ImGui::SameLine();
            const bool removable = key != "Prefab";
            ImGui::BeginDisabled(!removable);
            if (ImGui::SmallButton(ICON_MDI_DELETE_OUTLINE))
                removeRequested = true;
            ImGui::EndDisabled();
            if (!removable && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Prefab data is managed by the prefab instance.");

            ImGui::PopID();
            return open;
        }
    } // namespace

    InspectorWindow::InspectorWindow() : EditorWindow("Inspector", ICON_MDI_TUNE) {}

    void InspectorWindow::onClosed(EditorContext& ctx)
    {
        m_PreviewCache.clear(ctx);
        releaseModelPreviewRenderTarget(ctx);
        m_ModelPreviewWorld.clear();
        m_ModelPreviewRoot        = entt::null;
        m_ModelPreviewContentRoot = entt::null;
        m_ModelPreviewKey.clear();
    }

    void InspectorWindow::onDestroy(EditorContext& ctx)
    {
        m_PreviewCache.clear(ctx);
        m_TextureSelector.previewCache.clear(ctx);
        m_MeshSelector.previewCache.clear(ctx);
        releaseModelPreviewRenderTarget(ctx);
        m_ModelPreviewWorld.clear();
        m_ModelPreviewRoot        = entt::null;
        m_ModelPreviewContentRoot = entt::null;
        m_ModelPreviewKey.clear();
    }

    void InspectorWindow::draw(EditorContext& ctx)
    {
        const bool visible = ImGui::Begin(title().c_str(), &m_Open);
        if (!visible)
        {
            releaseModelPreviewRenderTarget(ctx);
            ImGui::End();
            return;
        }

        bool keepModelPreview = false;
        if (Selection::lastCategory() == SelectionCategory::Asset && ctx.services)
        {
            if (auto* assetService = ctx.services->tryGet<vultra::IAssetService>())
            {
                const auto entry = assetService->registry().lookup(Selection::lastId().native());
                keepModelPreview = entry.type == vasset::VAssetType::eMesh;
            }
        }
        else if (Selection::lastCategory() != SelectionCategory::Entity && !ctx.state.selectedSourceAsset.empty())
        {
            std::error_code ec;
            keepModelPreview = std::filesystem::is_regular_file(ctx.state.selectedSourceAsset, ec) &&
                               isModelSourceAsset(ctx.state.selectedSourceAsset);
        }

        if (!keepModelPreview && (!m_ModelPreviewKey.empty() || m_ModelPreviewTarget.texture ||
                                  m_ModelPreviewTarget.textureId || !m_RetiredModelPreviewTargets.empty()))
        {
            releaseModelPreviewRenderTarget(ctx);
            m_ModelPreviewWorld.clear();
            m_ModelPreviewRoot        = entt::null;
            m_ModelPreviewContentRoot = entt::null;
            m_ModelPreviewKey.clear();
        }

        if (Selection::lastCategory() == SelectionCategory::Entity)
            drawEntityInspector(ctx);
        else if (Selection::lastCategory() == SelectionCategory::Asset)
            drawAssetInspector(ctx);
        else if (!ctx.state.selectedSourceAsset.empty())
            drawSourceAssetInspector(ctx);
        else
        {
            auto& state = ctx.state;
            ImGui::TextUnformatted("Project");
            ImGui::Separator();
            ImGui::TextWrapped("Name: %s",
                               state.currentProjectName.empty() ? "(blank session)" : state.currentProjectName.c_str());
            ImGui::TextWrapped("Root: %s",
                               state.currentProject.empty() ? "(none)" : state.currentProject.generic_string().c_str());
            ImGui::TextWrapped("Asset root: %s", state.currentAssetRoot.c_str());
            ImGui::TextWrapped("Default scene: %s", state.currentDefaultScene.c_str());
        }

        ImGui::End();
    }

    void InspectorWindow::drawEntityInspector(EditorContext& ctx)
    {
        if (!ctx.services)
        {
            ImGui::TextUnformatted("Services are not available.");
            return;
        }

        auto* worldService = ctx.services->tryGet<vultra::IWorldService>();
        if (!worldService)
        {
            ImGui::TextUnformatted("World service is not available.");
            return;
        }

        auto& world = worldService->world();
        auto& reg   = world.registry();
        auto  e     = findEntityByUUID(world, Selection::lastId());
        if (e == entt::null || !reg.valid(e))
        {
            ImGui::TextUnformatted("Selected entity no longer exists.");
            return;
        }

        ui::sectionTitle(ICON_MDI_CUBE_OUTLINE, "Entity");

        if (auto* id = reg.try_get<vultra::IDComponent>(e))
            ImGui::TextWrapped("UUID: %s", id->uuid.toString().c_str());

        auto& name = reg.get_or_emplace<vultra::NameComponent>(e, vultra::NameComponent {"Entity"});
        if (m_NameEditEntity != Selection::lastId())
        {
            m_NameEditEntity = Selection::lastId();
            copyName(m_NameBuffer, name.name);
        }
        if (ImGui::InputText("Name", m_NameBuffer.data(), m_NameBuffer.size()))
        {
            name.name            = m_NameBuffer.data();
            ctx.state.sceneDirty = true;
            if (ctx.history)
                ctx.history->setNextLabel("Rename Entity");
        }

        auto& status = reg.get_or_emplace<vultra::EntityStatusComponent>(e);
        if (ImGui::CollapsingHeader("Status", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (drawMetaFields(&ctx, &m_TextureSelector, status))
            {
                ctx.state.sceneDirty = true;
                if (ctx.history)
                    ctx.history->setNextLabel("Edit Entity Status");
            }
        }

        if (componentHeader<vultra::HierarchyComponent>(world, e))
        {
            const auto* h = reg.try_get<vultra::HierarchyComponent>(e);
            ImGui::Text("Children: %u", h ? h->childCount : 0);
            if (h && h->parent != entt::null)
            {
                if (auto* parentId = reg.try_get<vultra::IDComponent>(h->parent))
                    ImGui::TextWrapped("Parent UUID: %s", parentId->uuid.toString().c_str());
            }
            else
            {
                ImGui::TextUnformatted("Parent: <scene root>");
            }
        }

        auto& componentOrder = m_ComponentOrder[Selection::lastId()];
        syncComponentOrder(reg, e, componentOrder);
        for (std::size_t i = 0; i < componentOrder.size(); ++i)
        {
            const std::string key               = componentOrder[i];
            bool              removeRequested   = false;
            bool              moveUpRequested   = false;
            bool              moveDownRequested = false;
            const bool        open              = orderedComponentHeader(
                key, i, componentOrder.size(), removeRequested, moveUpRequested, moveDownRequested);

            if (moveUpRequested && i > 0)
            {
                std::swap(componentOrder[i], componentOrder[i - 1]);
                ctx.state.statusMessage = "Moved component up: " + std::string(orderedComponentLabel(key));
                break;
            }
            if (moveDownRequested && i + 1 < componentOrder.size())
            {
                std::swap(componentOrder[i], componentOrder[i + 1]);
                ctx.state.statusMessage = "Moved component down: " + std::string(orderedComponentLabel(key));
                break;
            }

            if (removeRequested)
            {
                removeOrderedComponent(reg, e, key);
                componentOrder.erase(componentOrder.begin() + static_cast<std::ptrdiff_t>(i));
                ctx.state.sceneDirty    = true;
                ctx.state.statusMessage = "Removed component: " + std::string(orderedComponentLabel(key));
                if (ctx.history)
                    ctx.history->setNextLabel(ctx.state.statusMessage);
                break;
            }

            if (!open)
                continue;

            if (key == "Transform")
            {
                if (auto* transform = reg.try_get<vultra::TransformComponent>(e))
                {
                    const auto* entityId = reg.try_get<vultra::IDComponent>(e);
                    if (drawTransformComponentFields(*transform,
                                                     entityId ? entityId->uuid : vultra::CoreUUID {},
                                                     reg.try_get<vultra::LightComponent>(e)))
                    {
                        ctx.state.sceneDirty = true;
                        if (ctx.history)
                            ctx.history->setNextLabel("Edit Transform");
                    }
                }
            }
            else if (key == "Mesh")
            {
                if (auto* mesh = reg.try_get<vultra::MeshComponent>(e))
                    if (drawMeshComponentFields(&ctx, &m_TextureSelector, &m_MeshSelector, *mesh))
                    {
                        ctx.state.sceneDirty = true;
                        if (ctx.history)
                            ctx.history->setNextLabel("Edit Mesh");
                    }
            }
            else if (key == "Animator")
            {
                if (auto* animator = reg.try_get<vultra::AnimatorComponent>(e))
                    if (drawAnimatorComponentFields(ctx, *animator))
                    {
                        ctx.state.sceneDirty = true;
                        if (ctx.history)
                            ctx.history->setNextLabel("Edit Animator");
                    }
            }
            else if (key == "GaussianSplat")
            {
                if (auto* splat = reg.try_get<vultra::GaussianSplatComponent>(e))
                    if (drawMetaFields(&ctx, &m_TextureSelector, *splat))
                    {
                        ctx.state.sceneDirty = true;
                        if (ctx.history)
                            ctx.history->setNextLabel("Edit Gaussian Splat");
                    }
            }
            else if (key == "Environment")
            {
                if (auto* environment = reg.try_get<vultra::EnvironmentComponent>(e))
                    if (drawMetaFields(&ctx, &m_TextureSelector, *environment))
                    {
                        ctx.state.sceneDirty = true;
                        if (ctx.history)
                            ctx.history->setNextLabel("Edit Environment");
                    }
            }
            else if (key == "ReflectionProbe")
            {
                if (auto* probe = reg.try_get<vultra::ReflectionProbeComponent>(e))
                    if (drawMetaFields(&ctx, &m_TextureSelector, *probe))
                    {
                        ctx.state.sceneDirty = true;
                        if (ctx.history)
                            ctx.history->setNextLabel("Edit Reflection Probe");
                    }
            }
            else if (key == "Light")
            {
                if (auto* light = reg.try_get<vultra::LightComponent>(e))
                    if (drawLightComponentFields(*light))
                    {
                        ctx.state.sceneDirty = true;
                        if (ctx.history)
                            ctx.history->setNextLabel("Edit Light");
                    }
            }
            else if (key == "RigidBody")
            {
                if (auto* body = reg.try_get<vultra::RigidBodyComponent>(e))
                    if (drawMetaFields(&ctx, &m_TextureSelector, *body))
                    {
                        ctx.state.sceneDirty = true;
                        if (ctx.history)
                            ctx.history->setNextLabel("Edit Rigid Body");
                    }
            }
            else if (key == "BoxShape")
            {
                if (auto* shape = reg.try_get<vultra::BoxShapeComponent>(e))
                    if (drawMetaFields(&ctx, &m_TextureSelector, *shape))
                    {
                        ctx.state.sceneDirty = true;
                        if (ctx.history)
                            ctx.history->setNextLabel("Edit Box Shape");
                    }
            }
            else if (key == "SphereShape")
            {
                if (auto* shape = reg.try_get<vultra::SphereShapeComponent>(e))
                    if (drawMetaFields(&ctx, &m_TextureSelector, *shape))
                    {
                        ctx.state.sceneDirty = true;
                        if (ctx.history)
                            ctx.history->setNextLabel("Edit Sphere Shape");
                    }
            }
            else if (key == "CapsuleShape")
            {
                if (auto* shape = reg.try_get<vultra::CapsuleShapeComponent>(e))
                    if (drawMetaFields(&ctx, &m_TextureSelector, *shape))
                    {
                        ctx.state.sceneDirty = true;
                        if (ctx.history)
                            ctx.history->setNextLabel("Edit Capsule Shape");
                    }
            }
            else if (key == "Camera")
            {
                if (auto* camera = reg.try_get<vultra::CameraComponent>(e))
                {
                    if (ImGui::Button(ICON_MDI_CAMERA_SWITCH "  Align With Scene View",
                                      ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
                    {
                        if (alignCameraEntityToSceneView(ctx, world, e))
                        {
                            ctx.state.sceneDirty = true;
                            if (ctx.history)
                                ctx.history->setNextLabel("Align Camera");
                        }
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                        ImGui::SetTooltip("Move this Camera entity to the current Scene View camera pose.");
                    if (ImGui::Button(ICON_MDI_CROSSHAIRS_GPS "  Align Scene View With Camera",
                                      ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
                    {
                        alignSceneViewToCameraEntity(ctx, world, e);
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                        ImGui::SetTooltip("Move the Scene View editor camera to this Camera entity.");
                    ImGui::Spacing();

                    drawMetaFields<vultra::CameraComponent>(
                        &ctx, &m_TextureSelector, *camera, [&](const char* fieldName) {
                            if (std::strcmp(fieldName, "primary") == 0 && camera->primary)
                            {
                                auto view = reg.view<vultra::CameraComponent>();
                                for (auto other : view)
                                {
                                    if (other != e)
                                        view.get<vultra::CameraComponent>(other).primary = false;
                                }
                            }
                            if (camera->zFar <= camera->zNear)
                                camera->zFar = camera->zNear + 0.001f;
                            ctx.state.sceneDirty = true;
                            if (ctx.history)
                                ctx.history->setNextLabel("Edit Camera");
                        });
                }
            }
            else if (key == "XRView")
            {
                if (auto* xrView = reg.try_get<vultra::XRViewComponent>(e))
                    if (drawXRViewComponentFields(ctx, *xrView))
                    {
                        ctx.state.sceneDirty = true;
                        if (ctx.history)
                            ctx.history->setNextLabel("Edit XR View");
                    }
            }
            else if (key == "Script")
            {
                if (auto* script = reg.try_get<vultra::ScriptComponent>(e))
                    if (drawMetaFields(&ctx, &m_TextureSelector, *script))
                    {
                        ctx.state.sceneDirty = true;
                        if (ctx.history)
                            ctx.history->setNextLabel("Edit Script");
                    }
            }
            else if (key == "Prefab")
            {
                if (auto* prefab = reg.try_get<vultra::PrefabInstanceComponent>(e))
                {
                    ImGui::TextWrapped("URI: %s", prefab->prefabUri.c_str());
                    if (prefab->prefabId.valid())
                        ImGui::TextWrapped("UUID: %s", prefab->prefabId.toString().c_str());
                }
            }
        }

        ImGui::Spacing();
        drawAddComponentButton(ctx, world, e);
    }

    void InspectorWindow::drawAddComponentButton(EditorContext& ctx, vultra::World& world, const entt::entity entity)
    {
        auto& reg = world.registry();

        const float width = ImGui::GetContentRegionAvail().x;
        if (ImGui::Button(ICON_MDI_PLUS "  Add Component", ImVec2(width, 0.0f)))
            ImGui::OpenPopup("AddComponentPopup");

        if (ImGui::BeginPopup("AddComponentPopup"))
        {
            bool any = false;
            constexpr const char* categories[] = {
                "Core",
                "Rendering",
                "Animation",
                "Lighting",
                "Physics",
                "Camera",
                "Scripting",
            };
            for (const char* category : categories)
            {
                const auto categoryHasItems = std::any_of(addableComponents().begin(),
                                                          addableComponents().end(),
                                                          [&](const AddComponentDescriptor& desc) {
                                                              return desc.category && std::strcmp(desc.category, category) == 0 &&
                                                                     desc.has && desc.add && !desc.has(reg, entity);
                                                          });
                if (!categoryHasItems)
                    continue;

                any = true;
                if (ImGui::BeginMenu(category))
                {
                    for (const auto& desc : addableComponents())
                    {
                        if (!desc.category || std::strcmp(desc.category, category) != 0)
                            continue;
                        if (!desc.has || !desc.add || desc.has(reg, entity))
                            continue;

                        const bool xrViewRequiresCamera = desc.key && std::strcmp(desc.key, "XRView") == 0 &&
                                                          !reg.all_of<vultra::CameraComponent>(entity);
                        if (xrViewRequiresCamera)
                            ImGui::BeginDisabled();
                        if (ImGui::MenuItem(desc.label))
                        {
                            desc.add(ctx, reg, entity);
                            auto& order = m_ComponentOrder[Selection::lastId()];
                            if (desc.key && std::find(order.begin(), order.end(), desc.key) == order.end())
                                order.emplace_back(desc.key);
                            ctx.state.sceneDirty    = true;
                            ctx.state.statusMessage = std::string("Added component: ") + desc.label;
                            if (desc.key &&
                                (std::strcmp(desc.key, "BoxShape") == 0 ||
                                 std::strcmp(desc.key, "SphereShape") == 0 ||
                                 std::strcmp(desc.key, "CapsuleShape") == 0) &&
                                reg.all_of<vultra::MeshComponent>(entity))
                            {
                                ctx.state.statusMessage += " (fit to mesh bounds if available)";
                            }
                            if (ctx.history)
                                ctx.history->setNextLabel(ctx.state.statusMessage);
                            ImGui::CloseCurrentPopup();
                        }
                        if (xrViewRequiresCamera)
                        {
                            ImGui::EndDisabled();
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                                ImGui::SetTooltip("XR View can only be added to an entity with a Camera component.");
                        }
                    }
                    ImGui::EndMenu();
                }
            }

            if (!any)
                ImGui::TextDisabled("All addable components are already present.");

            ImGui::EndPopup();
        }
    }

    void InspectorWindow::drawAssetInspector(EditorContext& ctx)
    {
        if (!ctx.services)
        {
            ImGui::TextUnformatted("Services are not available.");
            return;
        }

        auto* assetService = ctx.services->tryGet<vultra::IAssetService>();
        if (!assetService)
        {
            ImGui::TextUnformatted("Asset service is not available.");
            return;
        }

        const auto uuid  = Selection::lastId();
        const auto entry = assetService->registry().lookup(uuid.native());

        ui::sectionTitle(ICON_MDI_PACKAGE_VARIANT_CLOSED, "Asset");
        ImGui::TextWrapped("UUID: %s", uuid.toString().c_str());
        ImGui::TextWrapped("Type: %s", vasset::toString(entry.type).c_str());
        ImGui::TextWrapped("Source: %s", entry.sourcePath.c_str());
        ImGui::TextWrapped("Imported: %s", entry.importedPath.c_str());

        if (entry.type == vasset::VAssetType::eMesh)
        {
            ImGui::Spacing();
            drawMeshAssetPreview(
                ctx, uuid, std::filesystem::path(entry.importedPath).filename().generic_string(), entry.importedPath);
        }
        else if (entry.type == vasset::VAssetType::eTexture)
        {
            ImGui::Spacing();
            drawTextureAssetPreview(ctx, entry);
        }
        else if (entry.type == vasset::VAssetType::eSkeleton)
        {
            ImGui::Spacing();
            drawSkeletonAssetInspector(ctx, entry);
        }
        else if (entry.type == vasset::VAssetType::eAnimation)
        {
            ImGui::Spacing();
            drawAnimationAssetInspector(ctx, entry);
        }
    }

    void InspectorWindow::drawTextureAssetPreview(EditorContext& ctx, const vasset::VAssetRegistry::AssetEntry& entry)
    {
        const std::string uri = !entry.sourcePath.empty() ?
                                    "res://" + std::filesystem::path(entry.sourcePath).generic_string() :
                                    "res://" + std::filesystem::path(entry.importedPath).generic_string();
        const auto previewId = m_PreviewCache.getTexturePreview(ctx, std::string_view(uri));
        if (!previewId)
        {
            drawImagePreviewPlaceholder(std::filesystem::path(entry.sourcePath), m_PreviewCache.lastError().c_str());
            return;
        }

        ImGui::TextUnformatted("Preview");
        const float size = std::min(ImGui::GetContentRegionAvail().x, 260.0f);
        ImGui::Image(previewId, ImVec2(size, size));
    }

    void InspectorWindow::drawSkeletonAssetInspector(EditorContext& ctx, const vasset::VAssetRegistry::AssetEntry& entry)
    {
        const auto assetRoot = ctx.state.currentProject / ctx.state.currentAssetRoot;
        const auto path = assetRoot / std::filesystem::path(entry.importedPath);
        vasset::VSkeleton skeleton;
        const auto result = vasset::loadSkeleton(path.generic_string(), skeleton);
        if (!result)
        {
            ImGui::TextDisabled("Skeleton metadata unavailable.");
            return;
        }

        ui::sectionTitle(ICON_MDI_SOURCE_BRANCH, "Skeleton");
        ImGui::TextWrapped("Name: %s", skeleton.name.c_str());
        ImGui::Text("Joints: %zu", skeleton.jointNames.size());
        ImGui::Text("Payload: %s", formatFileSize(skeleton.ozzData.size()).c_str());
    }

    void InspectorWindow::drawAnimationAssetInspector(EditorContext& ctx, const vasset::VAssetRegistry::AssetEntry& entry)
    {
        const auto assetRoot = ctx.state.currentProject / ctx.state.currentAssetRoot;
        const auto path = assetRoot / std::filesystem::path(entry.importedPath);
        vasset::VAnimation animation;
        const auto result = vasset::loadAnimation(path.generic_string(), animation);
        if (!result)
        {
            ImGui::TextDisabled("Animation metadata unavailable.");
            return;
        }

        ui::sectionTitle(ICON_MDI_PLAY, "Animation");
        ImGui::TextWrapped("Name: %s", animation.name.c_str());
        ImGui::Text("Duration: %.3f s", animation.duration);
        ImGui::Text("Payload: %s", formatFileSize(animation.ozzData.size()).c_str());
        ImGui::TextDisabled("Playback preview requires the runtime animation system.");
    }

    void InspectorWindow::drawSourceAssetInspector(EditorContext& ctx)
    {
        const auto& path = ctx.state.selectedSourceAsset;
        const auto  ext  = path.extension().generic_string();

        std::error_code ec;
        const bool      isDir = std::filesystem::is_directory(path, ec);
        ui::sectionTitle(ui::sourceAssetIcon(path, isDir), "Source Asset");
        ImGui::TextWrapped("Name: %s", path.filename().generic_string().c_str());
        ImGui::TextWrapped("Type: %s", ext.empty() ? "Folder" : ext.c_str());
        ImGui::TextWrapped("Path: %s", path.generic_string().c_str());

        if (std::filesystem::is_regular_file(path, ec))
            ImGui::Text("Size: %s", formatFileSize(std::filesystem::file_size(path, ec)).c_str());

        const bool renderGraphPassSource = std::filesystem::is_regular_file(path, ec) && fileLooksLikeRenderGraphPass(path);
        if (renderGraphPassSource)
        {
            ImGui::Spacing();
            drawRenderGraphPassSourceInspector(ctx, path);
        }

        if (std::filesystem::is_regular_file(path, ec) && isEditableSourceText(path))
        {
            const bool sceneSource = sourceAssetHasExtension(path, {".vscn"});
            const char* buttonText = sceneSource ? ICON_MDI_FILE_DOCUMENT_EDIT " Edit As Source" :
                                     renderGraphPassSource ? ICON_MDI_CODE_BRACES " Open Lua Source" :
                                                             ICON_MDI_FILE_DOCUMENT_EDIT " Open in Code Editor";
            if (ImGui::Button(buttonText))
            {
                ctx.state.codeEditorPath          = path.lexically_normal();
                ctx.state.codeEditorOpenRequested = true;
                ctx.state.statusMessage           = sceneSource ?
                                                        "Editing scene source: " + path.filename().generic_string() :
                                                        "Opened in Code Editor: " + path.filename().generic_string();
            }
        }

        if (ui::isTextureSourceAsset(path))
        {
            ImGui::Spacing();
            drawSourceTexturePreview(ctx, path);
        }
        else if (!isDir && isModelSourceAsset(path))
        {
            ImGui::Spacing();
            drawSourceModelPreview(ctx, path);
        }
        else if (sourceAssetHasExtension(path, {".vscn"}))
        {
            ImGui::Spacing();
            ImGui::TextUnformatted("Scene source");
            if (ImGui::Button("Set As Default Scene"))
            {
                const auto      assetRoot = ctx.state.currentProject / ctx.state.currentAssetRoot;
                std::error_code relEc;
                auto            rel = std::filesystem::relative(path, assetRoot, relEc);
                if (!relEc)
                {
                    ctx.state.currentDefaultScene = "res://" + rel.generic_string();
                    ctx.state.statusMessage       = "Default scene set to: " + ctx.state.currentDefaultScene;
                }
            }
        }
    }

    bool InspectorWindow::drawRenderGraphPassSourceInspector(EditorContext& ctx, const std::filesystem::path& path)
    {
        static RenderGraphPassEditState editState;
        if (editState.path != path.lexically_normal())
        {
            if (auto loaded = loadRenderGraphPassEditState(path))
                editState = std::move(*loaded);
            else
                editState = {};
        }

        if (!editState.valid)
            return false;

        ui::sectionTitle(ICON_MDI_VECTOR_POLYGON, "Render Graph Pass");

        bool dirty = false;
        dirty |= ImGui::InputText("Type", editState.type.data(), editState.type.size());

        const char* pipelines[] = {"Graphics", "Compute", "Raytracing"};
        dirty |= ImGui::Combo("Pipeline", &editState.pipeline, pipelines, IM_ARRAYSIZE(pipelines));

        dirty |= ImGui::InputText("Inputs", editState.inputs.data(), editState.inputs.size());
        dirty |= ImGui::InputText("Outputs", editState.outputs.data(), editState.outputs.size());

        if (editState.pipeline == 1)
        {
            dirty |= drawShaderLibrarySelector("Library", editState.library);
            dirty |= drawLibraryShaderSelector(ctx, "Compute", "comp", editState.library, editState.compute);
            dirty |= ImGui::Checkbox("Dispatch By Output Size", &editState.dispatchByOutputSize);
        }
        else if (editState.pipeline == 2)
        {
            dirty |= drawShaderLibrarySelector("Library", editState.library);
            dirty |= drawLibraryShaderSelector(ctx, "Raygen", "rgen", editState.library, editState.raygen);
            dirty |= drawLibraryShaderSelector(ctx, "Miss", "rmiss", editState.library, editState.miss, true);
            dirty |= drawLibraryShaderSelector(ctx, "Closest Hit", "rchit", editState.library, editState.closestHit, true);
            dirty |= drawLibraryShaderSelector(ctx, "Any Hit", "rahit", editState.library, editState.anyHit, true);
        }
        else
        {
            dirty |= drawShaderLibrarySelector("Vertex Library", editState.vertexLibrary);
            dirty |= drawShaderLibrarySelector("Fragment Library", editState.fragmentLibrary);
            dirty |= normalizeGraphicsShaderSelection(ctx, editState);

            const bool builtinVertex = std::string_view(editState.vertexLibrary.data()) == "builtin" ||
                                       editState.vertexLibrary[0] == '\0';
            if (builtinVertex)
                dirty |= drawBuiltinFullscreenVertexField(editState.vertex);
            else
                dirty |= drawShaderSelector(ctx, "Vertex", "vert", editState.vertex, false, false);

            if (std::string_view(editState.fragmentLibrary.data()) == "builtin")
                dirty |= drawShaderOptionSelector("Fragment", editState.fragment, collectBuiltinShaderIds("frag"));
            else
                dirty |= drawShaderSelector(ctx, "Fragment", "frag", editState.fragment, false, false);
            dirty |= normalizeGraphicsShaderSelection(ctx, editState);
        }

        static bool pendingUnsaved = false;
        dirty |= normalizePipelineShaderSelection(ctx, editState);
        if (dirty)
            pendingUnsaved = true;

        ImGui::BeginDisabled(!pendingUnsaved);
        if (ImGui::Button(ICON_MDI_CONTENT_SAVE " Save Pass"))
        {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file.is_open())
            {
                ctx.state.statusMessage = "Save render pass failed: cannot open file.";
            }
            else
            {
                static_cast<void>(normalizePipelineShaderSelection(ctx, editState));
                file << serializeRenderGraphPass(editState);
                file.close();
                if (!file)
                {
                    ctx.state.statusMessage = "Save render pass failed: cannot write file.";
                }
                else
                {
                    pendingUnsaved = false;
                    ++ctx.state.assetFileGeneration;
                    if (auto* assetService = ctx.services ? ctx.services->tryGet<vultra::IAssetService>() : nullptr)
                    {
                        if (auto uri = pathToResUri(ctx, path); !uri.empty())
                            (void)assetService->reimportAsset(uri, true);
                    }
                    ctx.state.statusMessage = "Saved render graph pass.";
                }
            }
        }
        ImGui::EndDisabled();
        if (pendingUnsaved)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("Unsaved");
        }

        return true;
    }

    void InspectorWindow::drawSourceTexturePreview(EditorContext& ctx, const std::filesystem::path& path)
    {
        const auto previewId = m_PreviewCache.getTexturePreview(ctx, path);
        if (!previewId)
        {
            drawImagePreviewPlaceholder(path, m_PreviewCache.lastError().c_str());
            return;
        }
        ImGui::TextUnformatted("Preview");
        const float size = std::min(ImGui::GetContentRegionAvail().x, 260.0f);
        ImGui::Image(previewId, ImVec2(size, size));
    }

    void InspectorWindow::drawSourceModelPreview(EditorContext& ctx, const std::filesystem::path& path)
    {
        const auto key = "source:" + path.lexically_normal().generic_string();
        if (m_ModelPreviewKey != key)
            rebuildModelPreviewWorldForSource(ctx, path);
        drawModelPreviewViewport(ctx, key);
    }

    void InspectorWindow::drawMeshAssetPreview(EditorContext&          ctx,
                                               const vultra::CoreUUID& uuid,
                                               const std::string&      name,
                                               const std::string&      importedPath)
    {
        const auto key = "mesh:" + uuid.toString() + ":" + importedPath;
        if (m_ModelPreviewKey != key)
            rebuildModelPreviewWorldForMesh(ctx, uuid, name, importedPath);
        drawModelPreviewViewport(ctx, key);
    }

    void InspectorWindow::drawModelPreviewViewport(EditorContext& ctx, const std::string& key)
    {
        ImGui::TextUnformatted("Preview");
        const float    width        = std::clamp(ImGui::GetContentRegionAvail().x, 140.0f, 220.0f);
        const float    height       = std::clamp(width * 0.68f, 120.0f, 180.0f);
        const uint32_t targetWidth  = quantizePreviewExtent(width);
        const uint32_t targetHeight = quantizePreviewExtent(height);
        if (targetWidth != m_ModelPreviewLastWidth || targetHeight != m_ModelPreviewLastHeight)
        {
            m_ModelPreviewLastWidth  = targetWidth;
            m_ModelPreviewLastHeight = targetHeight;
            m_ModelPreviewDirty      = true;
        }
        const glm::vec3 cameraOrbitDirection = glm::normalize(glm::vec3 {0.5f, 0.32f, 0.62f});
        const glm::vec3 viewForward          = -cameraOrbitDirection;
        glm::vec3       viewRight            = glm::cross(viewForward, glm::vec3 {0.0f, 1.0f, 0.0f});
        if (glm::dot(viewRight, viewRight) <= 1e-8f)
            viewRight = glm::vec3 {1.0f, 0.0f, 0.0f};
        else
            viewRight = glm::normalize(viewRight);
        const glm::vec3 viewUp          = glm::normalize(glm::cross(viewRight, viewForward));
        const auto      mapArcballWorld = [&](const ImVec2& mouse, const ImVec2& min, const ImVec2& max) {
            const glm::vec3 v = mapPreviewArcballPoint(mouse, min, max);
            return glm::normalize(v.x * viewRight + v.y * viewUp + v.z * cameraOrbitDirection);
        };
        ensureModelPreviewRenderTarget(ctx, targetWidth, targetHeight);

        if (!ctx.services || !m_ModelPreviewTarget.texture || !m_ModelPreviewTarget.textureId)
        {
            ImGui::BeginDisabled();
            ImGui::Button(ICON_MDI_CUBE_SCAN, ImVec2(width, height));
            ImGui::EndDisabled();
            return;
        }

        auto* worldService  = ctx.services->tryGet<vultra::IWorldService>();
        auto* assetService  = ctx.services->tryGet<vultra::IAssetService>();
        auto* cameraService = ctx.services->tryGet<vultra::ICameraService>();
        if (!worldService || !assetService || !cameraService)
        {
            ImGui::TextDisabled("Preview services are unavailable.");
            return;
        }

        (void)worldService;

        ImGui::Image(m_ModelPreviewTarget.textureId, ImVec2(width, height));
        const bool   hovered  = ImGui::IsItemHovered();
        const ImVec2 imageMin = ImGui::GetItemRectMin();
        const ImVec2 imageMax = ImGui::GetItemRectMax();

        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            m_ModelPreviewArcballActive = true;
            m_ModelPreviewArcballVector = mapArcballWorld(ImGui::GetIO().MousePos, imageMin, imageMax);
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            m_ModelPreviewArcballActive = false;
        if (m_ModelPreviewArcballActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
        {
            const glm::vec3 next = mapArcballWorld(ImGui::GetIO().MousePos, imageMin, imageMax);
            m_ModelPreviewRotation =
                glm::normalize(arcballDelta(m_ModelPreviewArcballVector, next) * m_ModelPreviewRotation);
            m_ModelPreviewArcballVector = next;
            m_ModelPreviewDirty         = true;
        }
        if (hovered)
        {
            const float wheel = ImGui::GetIO().MouseWheel;
            if (std::abs(wheel) > 0.0f)
            {
                m_ModelPreviewDistanceScale =
                    std::clamp(m_ModelPreviewDistanceScale * std::exp(-wheel * 0.16f), 0.12f, 12.0f);
                m_ModelPreviewDirty = true;
            }
        }

        auto* drawList = ImGui::GetWindowDrawList();
        drawList->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(72, 150, 225, 180), 4.0f);

        if (!previewWorldAssetsReady(m_ModelPreviewWorld, *assetService))
        {
            if (m_ModelPreviewCameraSubmitted)
            {
                cameraService->removeManualCamerasByName("Inspector Model Preview");
                m_ModelPreviewCameraSubmitted = false;
            }
            m_ModelPreviewDirty = true;
            return;
        }
        if (!m_ModelPreviewDirty)
        {
            if (m_ModelPreviewCameraSubmitted)
            {
                cameraService->removeManualCamerasByName("Inspector Model Preview");
                m_ModelPreviewCameraSubmitted = false;
            }
            return;
        }

        if (m_ModelPreviewRoot != entt::null && m_ModelPreviewWorld.registry().valid(m_ModelPreviewRoot))
        {
            auto& transform    = m_ModelPreviewWorld.registry().get<vultra::TransformComponent>(m_ModelPreviewRoot);
            transform.rotation = m_ModelPreviewRotation;
            transform.dirty    = true;
        }
        updateWorldTransforms(m_ModelPreviewWorld);
        const auto rotatedBounds = computeWorldMeshBounds(m_ModelPreviewWorld, *assetService);

        if (!rotatedBounds.valid)
        {
            ImGui::TextDisabled("Preview scene is empty.");
            m_ModelPreviewDirty = true;
            return;
        }

        const glm::vec3 center         = (rotatedBounds.min + rotatedBounds.max) * 0.5f;
        const glm::vec3 size           = rotatedBounds.max - rotatedBounds.min;
        const float     maxDimension   = std::max({size.x, size.y, size.z, 1.0f});
        const float     fovY           = glm::radians(45.0f);
        const float     baseDistance   = std::max(maxDimension * 1.6f, (maxDimension * 0.65f) / std::tan(fovY * 0.5f));
        const glm::vec3 cameraPosition = center + cameraOrbitDirection * baseDistance * m_ModelPreviewDistanceScale;
        const glm::vec3 keyLightDirection = glm::normalize(center - cameraPosition);
        setPreviewDirectionalLight(
            m_ModelPreviewWorld, "Preview Key Light", keyLightDirection, glm::vec3 {0.0f, -1.0f, 0.0f});
        updateWorldTransforms(m_ModelPreviewWorld);

        vultra::RenderCamera camera {};
        camera.name     = "Inspector Model Preview";
        camera.priority = 80;
        camera.view     = glm::lookAt(cameraPosition, center, glm::vec3 {0.0f, 1.0f, 0.0f});
        camera.projection =
            glm::perspectiveRH_ZO(fovY,
                                  static_cast<float>(targetWidth) / static_cast<float>(std::max(targetHeight, 1u)),
                                  std::max(0.01f, baseDistance - maxDimension * 1.5f),
                                  std::max(1000.0f, baseDistance * 8.0f));
        camera.zNear                   = std::max(0.01f, baseDistance - maxDimension * 1.5f);
        camera.zFar                    = std::max(1000.0f, baseDistance * 8.0f);
        camera.fovY                    = fovY;
        camera.target                  = &*m_ModelPreviewTarget.texture;
        camera.clearValue              = glm::vec4 {0.06f, 0.07f, 0.08f, 1.0f};
        camera.clearMode               = 0u;
        camera.suppressSkybox          = true;
        camera.renderImGui             = false;
        camera.rendererKey             = "universal";
        camera.selectionOutlineEnabled = false;
        camera.worldOverride           = &m_ModelPreviewWorld;
        camera.overrideFrameTime       = true;
        camera.frameTimeSeconds        = 0.0f;
        camera.frameDeltaSeconds       = 0.0f;
        cameraService->removeManualCamerasByName("Inspector Model Preview");
        cameraService->addManualCamera(camera);
        m_ModelPreviewDirty           = false;
        m_ModelPreviewCameraSubmitted = true;
    }

    void
    InspectorWindow::ensureModelPreviewRenderTarget(EditorContext& ctx, const uint32_t width, const uint32_t height)
    {
        if (!ctx.services || width == 0u || height == 0u)
            return;

        const auto  frame                  = static_cast<uint64_t>(ImGui::GetFrameCount());
        auto*       imguiServiceForCleanup = ctx.services->tryGet<vultra::IImGuiService>();
        std::size_t out                    = 0;
        for (auto& slot : m_RetiredModelPreviewTargets)
        {
            if (frame >= slot.releaseFrame)
            {
                if (imguiServiceForCleanup && slot.textureId)
                    imguiServiceForCleanup->removeTexture(slot.textureId);
                slot.texture.reset();
            }
            else
            {
                m_RetiredModelPreviewTargets[out++] = std::move(slot);
            }
        }
        m_RetiredModelPreviewTargets.resize(out);

        if (m_ModelPreviewTarget.texture && m_ModelPreviewTarget.extent.width == width &&
            m_ModelPreviewTarget.extent.height == height && m_ModelPreviewTarget.textureId)
        {
            return;
        }

        if (m_ModelPreviewTarget.texture || m_ModelPreviewTarget.textureId)
        {
            m_ModelPreviewTarget.releaseFrame = frame + kModelPreviewTargetReleaseDelayFrames;
            m_RetiredModelPreviewTargets.push_back(std::move(m_ModelPreviewTarget));
            m_ModelPreviewTarget = {};
        }

        auto* backendService = ctx.services->tryGet<vultra::IRenderBackendService>();
        auto* imguiService   = ctx.services->tryGet<vultra::IImGuiService>();
        if (!backendService || !imguiService)
            return;

        auto& rd     = backendService->renderDevice();
        auto  format = backendService->backbuffer().getPixelFormat();
        if (format == vultra::rhi::PixelFormat::eUndefined)
            format = vultra::rhi::PixelFormat::eRGBA8_UNorm;

        m_ModelPreviewTarget.extent = {width, height};
        m_ModelPreviewTarget.texture =
            vultra::rhi::Texture::Builder {}
                .setExtent(m_ModelPreviewTarget.extent)
                .setPixelFormat(format)
                .setNumMipLevels(1)
                .setUsageFlags(vultra::rhi::ImageUsage::eRenderTarget | vultra::rhi::ImageUsage::eSampled |
                               vultra::rhi::ImageUsage::eTransferSrc)
                .build(rd);
        if (m_ModelPreviewTarget.texture)
            m_ModelPreviewTarget.textureId = imguiService->addTexture(*m_ModelPreviewTarget.texture);
        m_ModelPreviewDirty = true;
    }

    void InspectorWindow::releaseModelPreviewRenderTarget(EditorContext& ctx)
    {
        if (m_ModelPreviewTarget.texture || !m_RetiredModelPreviewTargets.empty())
        {
            if (auto* backendService = ctx.services ? ctx.services->tryGet<vultra::IRenderBackendService>() : nullptr)
                backendService->renderDevice().waitIdle();
        }
        if (ctx.services)
        {
            if (auto* cameraService = ctx.services->tryGet<vultra::ICameraService>())
                cameraService->removeManualCamerasByName("Inspector Model Preview");
            if (auto* renderService = ctx.services->tryGet<vultra::IRenderService>())
                renderService->releaseOverrideRenderWorld(&m_ModelPreviewWorld);
            if (auto* imguiService = ctx.services->tryGet<vultra::IImGuiService>())
            {
                if (m_ModelPreviewTarget.textureId)
                    imguiService->removeTexture(m_ModelPreviewTarget.textureId);
                for (auto& slot : m_RetiredModelPreviewTargets)
                {
                    if (slot.textureId)
                        imguiService->removeTexture(slot.textureId);
                }
            }
        }
        m_ModelPreviewTarget = {};
        m_RetiredModelPreviewTargets.clear();
        m_ModelPreviewDirty           = true;
        m_ModelPreviewCameraSubmitted = false;
        m_ModelPreviewLastWidth       = 0;
        m_ModelPreviewLastHeight      = 0;
    }

    void InspectorWindow::rebuildModelPreviewWorldForSource(EditorContext& ctx, const std::filesystem::path& path)
    {
        if (ctx.services)
        {
            if (auto* renderService = ctx.services->tryGet<vultra::IRenderService>())
                renderService->releaseOverrideRenderWorld(&m_ModelPreviewWorld);
        }
        m_ModelPreviewWorld.clear();
        m_ModelPreviewRoot          = entt::null;
        m_ModelPreviewContentRoot   = entt::null;
        m_ModelPreviewKey           = "source:" + path.lexically_normal().generic_string();
        m_ModelPreviewPath          = path.lexically_normal();
        m_ModelPreviewRotation        = glm::quat {1.0f, 0.0f, 0.0f, 0.0f};
        m_ModelPreviewArcballVector   = glm::vec3 {0.0f, 0.0f, 1.0f};
        m_ModelPreviewArcballActive   = false;
        m_ModelPreviewCameraSubmitted = false;
        m_ModelPreviewDirty           = true;
        m_ModelPreviewDistanceScale   = 1.0f;

        addPreviewLighting(m_ModelPreviewWorld);
        m_ModelPreviewRoot = m_ModelPreviewWorld.createEntity();
        m_ModelPreviewWorld.registry().emplace<vultra::NameComponent>(m_ModelPreviewRoot,
                                                                      vultra::NameComponent {"Preview Model Pivot"});
        m_ModelPreviewContentRoot = m_ModelPreviewWorld.createChild(m_ModelPreviewRoot);
        m_ModelPreviewWorld.registry().emplace<vultra::NameComponent>(m_ModelPreviewContentRoot,
                                                                      vultra::NameComponent {"Preview Model Content"});
        if (!ctx.services)
            return;

        auto* assetService = ctx.services->tryGet<vultra::IAssetService>();
        auto* sceneService = ctx.services->tryGet<vultra::ISceneService>();
        if (!assetService || !sceneService)
            return;

        const auto      assetRoot = ctx.state.currentProject / ctx.state.currentAssetRoot;
        std::error_code ec;
        const auto      rel = std::filesystem::relative(path, assetRoot, ec);
        if (ec || rel.empty())
            return;

        const auto  relText = rel.generic_string();
        std::string manifestUri;
        for (const auto& [uuid, entry] : assetService->registry().getRegistry())
        {
            (void)uuid;
            if (entry.type == vasset::VAssetType::eSceneManifest && entry.sourcePath == relText &&
                !entry.importedPath.empty())
            {
                manifestUri = "res://" + entry.importedPath;
                break;
            }
        }

        if (!manifestUri.empty())
        {
            (void)sceneService->instantiateScene(m_ModelPreviewWorld, manifestUri, m_ModelPreviewContentRoot, false);
            centerPreviewContent(m_ModelPreviewWorld, *assetService, m_ModelPreviewContentRoot);
        }
    }

    void InspectorWindow::rebuildModelPreviewWorldForMesh(EditorContext&          ctx,
                                                          const vultra::CoreUUID& uuid,
                                                          const std::string&      name,
                                                          const std::string&      importedPath)
    {
        if (ctx.services)
        {
            if (auto* renderService = ctx.services->tryGet<vultra::IRenderService>())
                renderService->releaseOverrideRenderWorld(&m_ModelPreviewWorld);
        }
        m_ModelPreviewWorld.clear();
        m_ModelPreviewRoot        = entt::null;
        m_ModelPreviewContentRoot = entt::null;
        m_ModelPreviewKey         = "mesh:" + uuid.toString() + ":" + importedPath;
        m_ModelPreviewPath.clear();
        m_ModelPreviewRotation        = glm::quat {1.0f, 0.0f, 0.0f, 0.0f};
        m_ModelPreviewArcballVector   = glm::vec3 {0.0f, 0.0f, 1.0f};
        m_ModelPreviewArcballActive   = false;
        m_ModelPreviewCameraSubmitted = false;
        m_ModelPreviewDirty           = true;
        m_ModelPreviewDistanceScale   = 1.0f;

        addPreviewLighting(m_ModelPreviewWorld);
        m_ModelPreviewRoot = m_ModelPreviewWorld.createEntity();
        m_ModelPreviewWorld.registry().emplace<vultra::NameComponent>(m_ModelPreviewRoot,
                                                                      vultra::NameComponent {"Preview Mesh Pivot"});
        m_ModelPreviewContentRoot = m_ModelPreviewWorld.createChild(m_ModelPreviewRoot);
        m_ModelPreviewWorld.registry().emplace<vultra::NameComponent>(m_ModelPreviewContentRoot,
                                                                      vultra::NameComponent {"Preview Mesh Content"});
        auto  entity = m_ModelPreviewWorld.createChild(m_ModelPreviewContentRoot);
        auto& reg    = m_ModelPreviewWorld.registry();
        reg.emplace<vultra::NameComponent>(entity, vultra::NameComponent {name.empty() ? "Mesh Preview" : name});
        reg.emplace<vultra::MeshComponent>(entity, vultra::MeshComponent {.mesh = uuid});
        if (ctx.services)
        {
            if (auto* assetService = ctx.services->tryGet<vultra::IAssetService>())
                centerPreviewContent(m_ModelPreviewWorld, *assetService, m_ModelPreviewContentRoot);
        }
    }
} // namespace vultra_app
