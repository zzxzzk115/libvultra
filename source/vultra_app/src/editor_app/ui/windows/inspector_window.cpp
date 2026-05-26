#include "editor_app/ui/windows/inspector_window.hpp"

#include "common/ui_widgets.hpp"
#include "common/file_dialog.hpp"
#include "editor_app/selection.hpp"

#include <IconsMaterialDesignIcons.h>
#include <ImGuiFileDialog/ImGuiFileDialog.h>
#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/camera_service.hpp>
#include <vultra/function/services/imgui_service.hpp>
#include <vultra/function/services/render_backend_service.hpp>
#include <vultra/function/services/render_service.hpp>
#include <vultra/function/services/scene_service.hpp>
#include <vultra/function/services/world_service.hpp>
#include <vultra/function/rendering/render_structs.hpp>
#include <vultra/function/world/components/camera_component.hpp>
#include <vultra/function/world/components/environment_component.hpp>
#include <vultra/function/world/components/entity_status_component.hpp>
#include <vultra/function/world/components/gaussian_splat_component.hpp>
#include <vultra/function/world/components/hierarchy_component.hpp>
#include <vultra/function/world/components/id_component.hpp>
#include <vultra/function/world/components/light_component.hpp>
#include <vultra/function/world/components/mesh_component.hpp>
#include <vultra/function/world/components/name_component.hpp>
#include <vultra/function/world/components/prefab_instance_component.hpp>
#include <vultra/function/world/components/reflection_probe_component.hpp>
#include <vultra/function/world/components/script_component.hpp>
#include <vultra/function/world/components/transform_component.hpp>
#include <vultra/function/world/world.hpp>

#include <entt/meta/meta.hpp>
#include <glm/common.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <limits>
#include <unordered_map>
#include <string>
#include <string_view>
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
            std::transform(ext.begin(),
                           ext.end(),
                           ext.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
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
            std::transform(name.begin(),
                           name.end(),
                           name.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            std::transform(ext.begin(),
                           ext.end(),
                           ext.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

            const auto endsWith = [&](const char* suffix)
            {
                const std::string_view text(name);
                const std::string_view tail(suffix);
                return text.size() >= tail.size() && text.substr(text.size() - tail.size()) == tail;
            };

            return ext == ".lua" || ext == ".vshader" || ext == ".glsl" || ext == ".vert" || ext == ".frag" ||
                   ext == ".comp" || ext == ".json" || ext == ".vproject" || ext == ".vscn" || ext == ".txt" ||
                   ext == ".md" || endsWith(".vfeature.lua") || endsWith(".vsrp.lua") ||
                   endsWith(".vshaderlib.lua") || endsWith(".vso.lua");
        }

        void drawImagePreviewPlaceholder(const std::filesystem::path& path, const char* note)
        {
            ImGui::TextUnformatted("Preview");
            const float size = std::min(ImGui::GetContentRegionAvail().x, 220.0f);
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            ImDrawList*  dl  = ImGui::GetWindowDrawList();
            const ImVec2 max {pos.x + size, pos.y + size};

            const float tile = 16.0f;
            for (float y = pos.y; y < max.y; y += tile)
            {
                for (float x = pos.x; x < max.x; x += tile)
                {
                    const bool dark = (static_cast<int>((x - pos.x) / tile) + static_cast<int>((y - pos.y) / tile)) % 2 == 0;
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
            if (std::strcmp(metaName, "GaussianSplatComponent") == 0)
                return "Gaussian Splat";
            if (std::strcmp(metaName, "CameraComponent") == 0)
                return "Camera";
            if (std::strcmp(metaName, "EnvironmentComponent") == 0)
                return "Environment";
            if (std::strcmp(metaName, "ReflectionProbeComponent") == 0)
                return "Reflection Probe";
            if (std::strcmp(metaName, "LightComponent") == 0)
                return "Light";
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

        bool drawVec3Control(const char* label, glm::vec3& value, const glm::vec3& resetValue, const float speed = 0.05f)
        {
            bool changed = false;
            const auto cleanDisplayValue = [](float& v)
            {
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

            const float lineHeight = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
            const ImVec2 buttonSize {lineHeight + 3.0f, lineHeight};
            const float itemWidth = std::max(42.0f, (ImGui::GetContentRegionAvail().x - buttonSize.x * 3.0f -
                                                     ImGui::GetStyle().ItemSpacing.x * 6.0f) /
                                                        3.0f);

            auto axis = [&](const char* axisLabel,
                            float&      axisValue,
                            float       reset,
                            ImVec4      color,
                            ImVec4      hovered,
                            ImVec4      active)
            {
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
                changed |= ImGui::DragFloat((std::string("##") + axisLabel).c_str(), &axisValue, speed, 0.0f, 0.0f, "%.3f");
                ImGui::SameLine();
            };

            axis("X", value.x, resetValue.x, {0.55f, 0.16f, 0.18f, 1.0f}, {0.75f, 0.22f, 0.24f, 1.0f}, {0.9f, 0.28f, 0.32f, 1.0f});
            axis("Y", value.y, resetValue.y, {0.20f, 0.46f, 0.20f, 1.0f}, {0.28f, 0.64f, 0.28f, 1.0f}, {0.34f, 0.78f, 0.34f, 1.0f});
            axis("Z", value.z, resetValue.z, {0.16f, 0.28f, 0.58f, 1.0f}, {0.22f, 0.38f, 0.78f, 1.0f}, {0.30f, 0.48f, 0.94f, 1.0f});
            ImGui::NewLine();

            ImGui::Columns(1);
            ImGui::PopID();
            return changed;
        }

        glm::quat rotationFromDirection(const glm::vec3& direction);
        glm::vec3 directionFromTransform(const vultra::TransformComponent& transform);

        bool drawQuaternionDeltaControl(const char* label, glm::quat& rotation, const vultra::CoreUUID& entityId)
        {
            static std::unordered_map<vultra::CoreUUID, glm::vec3> s_RotationEditDegrees;

            auto [it, inserted] =
                s_RotationEditDegrees.try_emplace(entityId, glm::degrees(glm::eulerAngles(rotation)));
            auto& editDegrees = it->second;
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
            return true;
        }

        bool drawTransformComponentFields(vultra::TransformComponent& transform,
                                          const vultra::CoreUUID&     entityId,
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
                    changed            = true;
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
            bool changed = false;
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

            const auto local = makeTransformMatrix(*transform);
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
                min = valid ? glm::min(min, p) : p;
                max = valid ? glm::max(max, p) : p;
                valid = true;
            }
        };

        entt::entity findNamedEntity(vultra::World& world, const std::string& name)
        {
            auto& reg = world.registry();
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

        Bounds computeEntitySubtreeMeshBounds(vultra::World& world,
                                              vultra::IAssetService& assets,
                                              entt::entity root)
        {
            Bounds bounds;
            if (root == entt::null)
                return bounds;

            auto& reg = world.registry();
            auto  view = reg.view<vultra::TransformComponent, vultra::MeshComponent>();
            for (auto e : view)
            {
                if (!isDescendantOrSelf(world, e, root))
                    continue;

                const auto& meshComponent = view.get<vultra::MeshComponent>(e);
                if (!meshComponent.mesh.valid())
                    continue;

                auto mesh = assets.loadMeshSync(meshComponent.mesh);
                if (!mesh.ready() || !mesh.cpu())
                    continue;

                const auto worldMatrix = makeWorldTransformMatrix(reg, e);
                for (const auto& p : mesh.cpu()->positions)
                    bounds.include(glm::vec3(worldMatrix * glm::vec4(glm::vec3 {p.x, p.y, p.z}, 1.0f)));
            }
            return bounds;
        }

        Bounds computeWorldMeshBounds(vultra::World& world, vultra::IAssetService& assets)
        {
            Bounds bounds;
            auto&  reg = world.registry();
            auto   view = reg.view<vultra::TransformComponent, vultra::MeshComponent>();
            for (auto e : view)
            {
                const auto& meshComponent = view.get<vultra::MeshComponent>(e);
                if (!meshComponent.mesh.valid())
                    continue;
                auto mesh = assets.loadMeshSync(meshComponent.mesh);
                if (!mesh.ready() || !mesh.cpu())
                    continue;
                const auto worldMatrix = makeWorldTransformMatrix(reg, e);
                for (const auto& p : mesh.cpu()->positions)
                    bounds.include(glm::vec3(worldMatrix * glm::vec4(glm::vec3 {p.x, p.y, p.z}, 1.0f)));
            }
            return bounds;
        }

        void updateWorldTransforms(vultra::World& world)
        {
            auto& reg = world.registry();
            std::vector<entt::entity> roots;
            auto view = reg.view<vultra::TransformComponent, vultra::HierarchyComponent>();
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
                    t->dirty = false;
                }
                for (auto child = h->firstChild; child != entt::null; child = world.nextSibling(child))
                    stack.push_back({child, t->worldMatrix, dirty});
            }
        }

        void addPreviewLighting(vultra::World& world)
        {
            auto& reg = world.registry();
            auto keyLight = world.createEntity();
            reg.emplace<vultra::NameComponent>(keyLight, vultra::NameComponent {"Preview Key Light"});
            reg.emplace<vultra::LightComponent>(keyLight,
                                                vultra::LightComponent {
                                                    .kind = 0u,
                                                    .color = glm::vec3 {1.0f},
                                                    .intensity = 6.0f,
                                                    .castsShadow = false,
                                                });

            auto env = world.createEntity();
            reg.emplace<vultra::NameComponent>(env, vultra::NameComponent {"Preview Environment"});
            reg.emplace<vultra::EnvironmentComponent>(env,
                                                      vultra::EnvironmentComponent {
                                                          .ambientColor = glm::vec3 {0.28f, 0.30f, 0.34f},
                                                          .ambientIntensity = 1.4f,
                                                          .enableIBL = false,
                                                      });
        }

        entt::entity findNamedEntity(vultra::World& world, const char* name)
        {
            auto& reg = world.registry();
            auto  view = reg.view<vultra::NameComponent>();
            for (auto e : view)
            {
                if (view.get<vultra::NameComponent>(e).name == name)
                    return e;
            }
            return entt::null;
        }

        void setPreviewDirectionalLight(vultra::World& world,
                                        const char*    name,
                                        glm::vec3      direction,
                                        const glm::vec3& fallback)
        {
            auto& reg = world.registry();
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

            auto& transform = reg.get<vultra::TransformComponent>(entity);
            transform.rotation = glm::normalize(glm::quatLookAtRH(direction, up));
            transform.dirty = true;
        }

        void centerPreviewContent(vultra::World&              world,
                                  vultra::IAssetService&      assets,
                                  const entt::entity          contentRoot)
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
            const float width = std::max(1.0f, max.x - min.x);
            const float height = std::max(1.0f, max.y - min.y);
            const float diameter = std::max(1.0f, std::min(width, height));
            const float x = (2.0f * (mouse.x - (min.x + width * 0.5f))) / diameter;
            const float y = (-2.0f * (mouse.y - (min.y + height * 0.5f))) / diameter;
            const float len2 = x * x + y * y;

            if (len2 <= 1.0f)
                return glm::normalize(glm::vec3 {x, y, std::sqrt(std::max(0.0f, 1.0f - len2))});

            const float invLen = 1.0f / std::sqrt(len2);
            return glm::vec3 {x * invLen, y * invLen, 0.0f};
        }

        glm::quat arcballDelta(const glm::vec3& from, const glm::vec3& to)
        {
            const glm::vec3 axis = glm::cross(from, to);
            const float     axisLen2 = glm::dot(axis, axis);
            if (axisLen2 <= 1e-8f)
                return glm::quat {1.0f, 0.0f, 0.0f, 0.0f};

            const float dot = std::clamp(glm::dot(from, to), -1.0f, 1.0f);
            return glm::normalize(glm::angleAxis(std::acos(dot), axis * glm::inversesqrt(axisLen2)));
        }

        uint32_t quantizePreviewExtent(const float size)
        {
            constexpr uint32_t kStep = 32u;
            const auto pixels = static_cast<uint32_t>(std::ceil(std::max(1.0f, size)));
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

            const auto& camera = reg.get<vultra::CameraComponent>(entity);
            ctx.state.sceneCameraAlignRequest.pending     = true;
            ctx.state.sceneCameraAlignRequest.position    = glm::vec3(worldTransform[3]);
            ctx.state.sceneCameraAlignRequest.rotation    = extractRotation(worldTransform);
            ctx.state.sceneCameraAlignRequest.fovYDegrees = camera.projection == 0u ? camera.fovYDegrees :
                                                                                ctx.state.sceneCamera.fovYDegrees;
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
            const auto assetRoot = editorAssetRoot(ctx);
            std::error_code ec;
            const auto rel = std::filesystem::relative(path, assetRoot, ec);
            if (ec || rel.empty())
                return {};
            return "res://" + rel.generic_string();
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

                    any = true;
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

        bool drawUuidObjectField(EditorContext* ctx,
                                 vultra::CoreUUID& uuid,
                                 const char*       fieldName,
                                 const char*       label)
        {
            bool changed = false;
            const auto uri = assetUuidToUri(ctx, uuid);
            const auto text = uuid.valid() ? (!uri.empty() ? uri : uuid.toString())
                                           : std::string(ICON_MDI_BULLSEYE "  None (") +
                                                 expectedAssetLabelForField(fieldName) + ")";
            const auto dialogKey = std::string("InspectorSelectAsset_") + fieldName;

            ImGui::TextUnformatted(label);
            ImGui::PushID(label);
            const float buttonSize = ImGui::GetFrameHeight();
            const float spacing    = ImGui::GetStyle().ItemSpacing.x;
            const float fieldWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x - buttonSize * 2.0f - spacing * 2.0f);
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
                            uuid     = dropped;
                            changed  = true;
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
                uuid     = {};
                changed  = true;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Clear reference.");

            changed |= drawAssetRegistryPicker(ctx, dialogKey.c_str(), expectedAssetTypeForField(fieldName), uuid);
            ImGui::PopID();

            return changed;
        }

        bool drawScriptUriObjectField(EditorContext* ctx, std::string& uri, const char* label)
        {
            bool changed = false;
            const auto text = uri.empty() ? std::string(ICON_MDI_BULLSEYE "  None (Lua Script)")
                                          : std::string(ICON_MDI_LANGUAGE_LUA "  ") + uri;

            ImGui::TextUnformatted(label);
            ImGui::PushID(label);
            const float buttonSize = ImGui::GetFrameHeight();
            const float fieldWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x - buttonSize - ImGui::GetStyle().ItemSpacing.x);
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
                            auto* assetService = ctx->services->tryGet<vultra::IAssetService>();
                            const auto entry =
                                assetService ? assetService->registry().lookup(dropped.native())
                                             : vasset::VAssetRegistry::AssetEntry {};
                            if (entry.type == vasset::VAssetType::eScriptLua)
                            {
                                const auto resolvedUri = assetUuidToUri(ctx, dropped);
                                if (!resolvedUri.empty())
                                {
                                    uri      = resolvedUri;
                                    changed  = true;
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
            if (ctx && ImGuiFileDialog::Instance()->Display(kScriptDialogKey,
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
                            uri      = selectedUri;
                            changed  = true;
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

        bool drawRendererKeyCombo(EditorContext* ctx, std::string& rendererKey, const char* label)
        {
            auto* renderService = ctx && ctx->services ? ctx->services->tryGet<vultra::IRenderService>() : nullptr;
            auto* renderBackend = ctx && ctx->services ? ctx->services->tryGet<vultra::IRenderBackendService>() : nullptr;
            auto  keys          = renderService ? renderService->rendererKeys() : std::vector<std::string> {};

            keys.erase(std::remove(keys.begin(), keys.end(), "editor-shell"), keys.end());

            if (rendererKey.empty())
                rendererKey = "universal";

            if (std::find(keys.begin(), keys.end(), rendererKey) == keys.end())
                keys.push_back(rendererKey);

            std::sort(keys.begin(), keys.end());
            keys.erase(std::unique(keys.begin(), keys.end()), keys.end());

            bool changed = false;
            const char* preview = rendererKey.empty() ? "<none>" : rendererKey.c_str();
            if (ImGui::BeginCombo(label, preview))
            {
                const bool rayTracingAvailable =
                    renderBackend &&
                    HasFlagValues(renderBackend->renderDevice().getFeatureFlag(),
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

        bool drawMetaValue(EditorContext*          ctx,
                           const entt::meta_data& field,
                           entt::meta_any&        value,
                           const char*            fieldName,
                           const char*            label)
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
                return drawUuidObjectField(ctx, *v, fieldName, label);

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
        bool shouldDrawMetaField(const vultra::EnvironmentComponent& environment, const char* fieldName)
        {
            if (std::strcmp(fieldName, "iblColor") == 0 || std::strcmp(fieldName, "iblIntensity") == 0)
                return environment.enableIBL;
            return true;
        }

        template<>
        bool shouldDrawMetaField(const vultra::ReflectionProbeComponent& probe, const char* fieldName)
        {
            if (std::strcmp(fieldName, "environmentMap") == 0 ||
                std::strcmp(fieldName, "intensity") == 0 ||
                std::strcmp(fieldName, "parallaxCorrection") == 0)
                return probe.enableIBL;
            if (std::strcmp(fieldName, "boxSize") == 0)
                return probe.shape == 0u;
            if (std::strcmp(fieldName, "radius") == 0)
                return probe.shape == 1u;
            return true;
        }

        template<typename Component>
        bool drawMetaFields(EditorContext* ctx,
                            Component&     component,
                            const std::function<void(const char*)>& onChanged = {})
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
                if (drawMetaValue(ctx, field, value, rawName, label.c_str()))
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

        template<typename Component>
        const char* componentLabel()
        {
            const char* metaName = entt::resolve<Component>().name();
            const char* label    = (metaName != nullptr) ? displayComponentName(metaName) : componentDisplayName<Component>();
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
        void drawReflectedComponent(vultra::World& world,
                                    entt::entity   entity,
                                    EditorContext* ctx,
                                    const std::function<void(Component&, const char*)>& onChanged = {})
        {
            if (!componentHeader<Component>(world, entity))
                return;

            auto& component = world.registry().get<Component>(entity);
            drawMetaFields<Component>(
                ctx,
                component,
                [&](const char* fieldName)
                {
                    if (onChanged)
                        onChanged(component, fieldName);
                });
        }

        struct AddComponentDescriptor
        {
            const char* key {};
            const char* label {};
            bool (*has)(entt::registry&, entt::entity) {};
            void (*add)(entt::registry&, entt::entity) {};
        };

        template<typename Component>
        AddComponentDescriptor addComponentDescriptor(const char* key, const char* label)
        {
            return AddComponentDescriptor {
                key,
                label,
                [](entt::registry& reg, entt::entity entity) { return reg.all_of<Component>(entity); },
                [](entt::registry& reg, entt::entity entity)
                {
                    if (!reg.all_of<Component>(entity))
                        reg.emplace<Component>(entity);
                },
            };
        }

        const std::vector<AddComponentDescriptor>& addableComponents()
        {
            static const std::vector<AddComponentDescriptor> descriptors {
                addComponentDescriptor<vultra::TransformComponent>("Transform", "Transform"),
                addComponentDescriptor<vultra::CameraComponent>("Camera", "Camera"),
                addComponentDescriptor<vultra::EnvironmentComponent>("Environment", "Environment"),
                addComponentDescriptor<vultra::ReflectionProbeComponent>("ReflectionProbe", "Reflection Probe"),
                addComponentDescriptor<vultra::LightComponent>("Light", "Light"),
                addComponentDescriptor<vultra::MeshComponent>("Mesh", "Mesh"),
                addComponentDescriptor<vultra::GaussianSplatComponent>("GaussianSplat", "Gaussian Splat"),
                addComponentDescriptor<vultra::ScriptComponent>("Script", "Script"),
            };
            return descriptors;
        }

        const std::vector<const char*>& componentDefaultOrder()
        {
            static const std::vector<const char*> order {
                "Transform",
                "Mesh",
                "GaussianSplat",
                "Environment",
                "ReflectionProbe",
                "Light",
                "Camera",
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
            if (key == "GaussianSplat")
                return reg.all_of<vultra::GaussianSplatComponent>(entity);
            if (key == "Environment")
                return reg.all_of<vultra::EnvironmentComponent>(entity);
            if (key == "ReflectionProbe")
                return reg.all_of<vultra::ReflectionProbeComponent>(entity);
            if (key == "Light")
                return reg.all_of<vultra::LightComponent>(entity);
            if (key == "Camera")
                return reg.all_of<vultra::CameraComponent>(entity);
            if (key == "Script")
                return reg.all_of<vultra::ScriptComponent>(entity);
            if (key == "Prefab")
                return reg.all_of<vultra::PrefabInstanceComponent>(entity);
            return false;
        }

        void syncComponentOrder(entt::registry& reg, entt::entity entity, std::vector<std::string>& order)
        {
            order.erase(std::remove_if(order.begin(),
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
            if (key == "GaussianSplat")
                return "Gaussian Splat";
            if (key == "Environment")
                return "Environment";
            if (key == "ReflectionProbe")
                return "Reflection Probe";
            if (key == "Light")
                return "Light";
            if (key == "Camera")
                return "Camera";
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
            else if (key == "GaussianSplat")
                reg.remove<vultra::GaussianSplatComponent>(entity);
            else if (key == "Environment")
                reg.remove<vultra::EnvironmentComponent>(entity);
            else if (key == "ReflectionProbe")
                reg.remove<vultra::ReflectionProbeComponent>(entity);
            else if (key == "Light")
                reg.remove<vultra::LightComponent>(entity);
            else if (key == "Camera")
                reg.remove<vultra::CameraComponent>(entity);
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
                                                      ImGuiTreeNodeFlags_DefaultOpen |
                                                          ImGuiTreeNodeFlags_AllowOverlap);

            const ImGuiStyle& style = ImGui::GetStyle();
            const float upWidth     = ImGui::CalcTextSize(ICON_MDI_ARROW_UP).x + style.FramePadding.x * 2.0f;
            const float downWidth   = ImGui::CalcTextSize(ICON_MDI_ARROW_DOWN).x + style.FramePadding.x * 2.0f;
            const float deleteWidth = ImGui::CalcTextSize(ICON_MDI_DELETE_OUTLINE).x + style.FramePadding.x * 2.0f;
            const float buttonWidth = upWidth + downWidth + deleteWidth + style.ItemSpacing.x * 2.0f;
            const float rightX      = ImGui::GetWindowContentRegionMax().x - buttonWidth;
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
        m_ModelPreviewRoot = entt::null;
        m_ModelPreviewContentRoot = entt::null;
        m_ModelPreviewKey.clear();
    }

    void InspectorWindow::onDestroy(EditorContext& ctx)
    {
        m_PreviewCache.clear(ctx);
        releaseModelPreviewRenderTarget(ctx);
        m_ModelPreviewWorld.clear();
        m_ModelPreviewRoot = entt::null;
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
            m_ModelPreviewRoot = entt::null;
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
            ImGui::TextWrapped("Name: %s", state.currentProjectName.empty() ? "(blank session)" : state.currentProjectName.c_str());
            ImGui::TextWrapped("Root: %s", state.currentProject.empty() ? "(none)" : state.currentProject.generic_string().c_str());
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
            name.name = m_NameBuffer.data();
            ctx.state.sceneDirty = true;
        }

        auto& status = reg.get_or_emplace<vultra::EntityStatusComponent>(e);
        if (ImGui::CollapsingHeader("Status", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (drawMetaFields(&ctx, status))
                ctx.state.sceneDirty = true;
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
            const std::string key = componentOrder[i];
            bool removeRequested = false;
            bool moveUpRequested = false;
            bool moveDownRequested = false;
            const bool open = orderedComponentHeader(
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
                ctx.state.sceneDirty = true;
                ctx.state.statusMessage = "Removed component: " + std::string(orderedComponentLabel(key));
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
                        ctx.state.sceneDirty = true;
                }
            }
            else if (key == "Mesh")
            {
                if (auto* mesh = reg.try_get<vultra::MeshComponent>(e))
                    if (drawMetaFields(&ctx, *mesh))
                        ctx.state.sceneDirty = true;
            }
            else if (key == "GaussianSplat")
            {
                if (auto* splat = reg.try_get<vultra::GaussianSplatComponent>(e))
                    if (drawMetaFields(&ctx, *splat))
                        ctx.state.sceneDirty = true;
            }
            else if (key == "Environment")
            {
                if (auto* environment = reg.try_get<vultra::EnvironmentComponent>(e))
                    if (drawMetaFields(&ctx, *environment))
                        ctx.state.sceneDirty = true;
            }
            else if (key == "ReflectionProbe")
            {
                if (auto* probe = reg.try_get<vultra::ReflectionProbeComponent>(e))
                    if (drawMetaFields(&ctx, *probe))
                        ctx.state.sceneDirty = true;
            }
            else if (key == "Light")
            {
                if (auto* light = reg.try_get<vultra::LightComponent>(e))
                    if (drawLightComponentFields(*light))
                        ctx.state.sceneDirty = true;
            }
            else if (key == "Camera")
            {
                if (auto* camera = reg.try_get<vultra::CameraComponent>(e))
                {
                    if (ImGui::Button(ICON_MDI_CAMERA_SWITCH "  Align With Scene View",
                                      ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
                    {
                        if (alignCameraEntityToSceneView(ctx, world, e))
                            ctx.state.sceneDirty = true;
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
                        &ctx,
                        *camera,
                        [&](const char* fieldName)
                        {
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
                        });
                }
            }
            else if (key == "Script")
            {
                if (auto* script = reg.try_get<vultra::ScriptComponent>(e))
                    if (drawMetaFields(&ctx, *script))
                        ctx.state.sceneDirty = true;
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
            for (const auto& desc : addableComponents())
            {
                if (!desc.has || !desc.add || desc.has(reg, entity))
                    continue;

                any = true;
                if (ImGui::MenuItem(desc.label))
                {
                    desc.add(reg, entity);
                    auto& order = m_ComponentOrder[Selection::lastId()];
                    if (desc.key && std::find(order.begin(), order.end(), desc.key) == order.end())
                        order.emplace_back(desc.key);
                    ctx.state.sceneDirty = true;
                    ctx.state.statusMessage = std::string("Added component: ") + desc.label;
                    ImGui::CloseCurrentPopup();
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
            drawMeshAssetPreview(ctx, uuid, std::filesystem::path(entry.importedPath).filename().generic_string(), entry.importedPath);
        }
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

        if (std::filesystem::is_regular_file(path, ec) && isEditableSourceText(path))
        {
            if (ImGui::Button(ICON_MDI_FILE_DOCUMENT_EDIT " Open in Code Editor"))
            {
                ctx.state.codeEditorPath          = path.lexically_normal();
                ctx.state.codeEditorOpenRequested = true;
                ctx.state.statusMessage           = "Opened in Code Editor: " + path.filename().generic_string();
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
                const auto assetRoot = ctx.state.currentProject / ctx.state.currentAssetRoot;
                std::error_code relEc;
                auto rel = std::filesystem::relative(path, assetRoot, relEc);
                if (!relEc)
                {
                    ctx.state.currentDefaultScene = "res://" + rel.generic_string();
                    ctx.state.statusMessage       = "Default scene set to: " + ctx.state.currentDefaultScene;
                }
            }
        }
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

    void InspectorWindow::drawMeshAssetPreview(EditorContext& ctx,
                                               const vultra::CoreUUID& uuid,
                                               const std::string& name,
                                               const std::string& importedPath)
    {
        const auto key = "mesh:" + uuid.toString() + ":" + importedPath;
        if (m_ModelPreviewKey != key)
            rebuildModelPreviewWorldForMesh(ctx, uuid, name, importedPath);
        drawModelPreviewViewport(ctx, key);
    }

    void InspectorWindow::drawModelPreviewViewport(EditorContext& ctx, const std::string& key)
    {
        ImGui::TextUnformatted("Preview");
        const float width = std::max(160.0f, ImGui::GetContentRegionAvail().x);
        const float height = std::clamp(width * 0.62f, 140.0f, 260.0f);
        const uint32_t targetWidth = quantizePreviewExtent(width);
        const uint32_t targetHeight = quantizePreviewExtent(height);
        const glm::vec3 cameraOrbitDirection = glm::normalize(glm::vec3 {0.5f, 0.32f, 0.62f});
        const glm::vec3 viewForward = -cameraOrbitDirection;
        glm::vec3       viewRight = glm::cross(viewForward, glm::vec3 {0.0f, 1.0f, 0.0f});
        if (glm::dot(viewRight, viewRight) <= 1e-8f)
            viewRight = glm::vec3 {1.0f, 0.0f, 0.0f};
        else
            viewRight = glm::normalize(viewRight);
        const glm::vec3 viewUp = glm::normalize(glm::cross(viewRight, viewForward));
        const auto mapArcballWorld = [&](const ImVec2& mouse, const ImVec2& min, const ImVec2& max)
        {
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

        auto* worldService = ctx.services->tryGet<vultra::IWorldService>();
        auto* assetService = ctx.services->tryGet<vultra::IAssetService>();
        auto* cameraService = ctx.services->tryGet<vultra::ICameraService>();
        if (!worldService || !assetService || !cameraService)
        {
            ImGui::TextDisabled("Preview services are unavailable.");
            return;
        }

        (void)worldService;

        ImGui::Image(m_ModelPreviewTarget.textureId, ImVec2(width, height));
        const bool hovered = ImGui::IsItemHovered();
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
            m_ModelPreviewRotation = glm::normalize(arcballDelta(m_ModelPreviewArcballVector, next) *
                                                    m_ModelPreviewRotation);
            m_ModelPreviewArcballVector = next;
        }
        if (hovered)
        {
            const float wheel = ImGui::GetIO().MouseWheel;
            if (std::abs(wheel) > 0.0f)
                m_ModelPreviewDistanceScale = std::clamp(m_ModelPreviewDistanceScale * std::exp(-wheel * 0.16f),
                                                         0.12f,
                                                         12.0f);
        }

        if (m_ModelPreviewRoot != entt::null && m_ModelPreviewWorld.registry().valid(m_ModelPreviewRoot))
        {
            auto& transform = m_ModelPreviewWorld.registry().get<vultra::TransformComponent>(m_ModelPreviewRoot);
            transform.rotation = m_ModelPreviewRotation;
            transform.dirty = true;
        }
        updateWorldTransforms(m_ModelPreviewWorld);
        const auto rotatedBounds = computeWorldMeshBounds(m_ModelPreviewWorld, *assetService);

        auto* drawList = ImGui::GetWindowDrawList();
        drawList->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(72, 150, 225, 180), 4.0f);

        if (!rotatedBounds.valid)
        {
            ImGui::TextDisabled("Preview scene is empty.");
            return;
        }

        const glm::vec3 center = (rotatedBounds.min + rotatedBounds.max) * 0.5f;
        const glm::vec3 size = rotatedBounds.max - rotatedBounds.min;
        const float maxDimension = std::max({size.x, size.y, size.z, 1.0f});
        const float fovY = glm::radians(45.0f);
        const float baseDistance = std::max(maxDimension * 1.6f, (maxDimension * 0.65f) / std::tan(fovY * 0.5f));
        const glm::vec3 cameraPosition = center + cameraOrbitDirection * baseDistance * m_ModelPreviewDistanceScale;
        const glm::vec3 keyLightDirection = glm::normalize(center - cameraPosition);
        setPreviewDirectionalLight(m_ModelPreviewWorld,
                                   "Preview Key Light",
                                   keyLightDirection,
                                   glm::vec3 {0.0f, -1.0f, 0.0f});
        updateWorldTransforms(m_ModelPreviewWorld);

        vultra::RenderCamera camera {};
        camera.name = "Inspector Model Preview";
        camera.priority = 80;
        camera.view = glm::lookAt(cameraPosition, center, glm::vec3 {0.0f, 1.0f, 0.0f});
        camera.projection = glm::perspectiveRH_ZO(fovY,
                                                  static_cast<float>(targetWidth) /
                                                      static_cast<float>(std::max(targetHeight, 1u)),
                                                  std::max(0.01f, baseDistance - maxDimension * 1.5f),
                                                  std::max(1000.0f, baseDistance * 8.0f));
        camera.zNear = std::max(0.01f, baseDistance - maxDimension * 1.5f);
        camera.zFar = std::max(1000.0f, baseDistance * 8.0f);
        camera.fovY = fovY;
        camera.target = &*m_ModelPreviewTarget.texture;
        camera.clearValue = glm::vec4 {0.06f, 0.07f, 0.08f, 1.0f};
        camera.clearMode = 0u;
        camera.renderImGui = false;
        camera.rendererKey = "universal";
        camera.selectionOutlineEnabled = false;
        camera.worldOverride = &m_ModelPreviewWorld;
        cameraService->addManualCamera(camera);
    }

    void InspectorWindow::ensureModelPreviewRenderTarget(EditorContext& ctx, const uint32_t width, const uint32_t height)
    {
        if (!ctx.services || width == 0u || height == 0u)
            return;

        const auto frame = static_cast<uint64_t>(ImGui::GetFrameCount());
        auto*      imguiServiceForCleanup = ctx.services->tryGet<vultra::IImGuiService>();
        std::size_t out = 0;
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
        auto* imguiService = ctx.services->tryGet<vultra::IImGuiService>();
        if (!backendService || !imguiService)
            return;

        auto& rd = backendService->renderDevice();
        auto format = backendService->backbuffer().getPixelFormat();
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
    }

    void InspectorWindow::releaseModelPreviewRenderTarget(EditorContext& ctx)
    {
        if (ctx.services)
        {
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
    }

    void InspectorWindow::rebuildModelPreviewWorldForSource(EditorContext& ctx, const std::filesystem::path& path)
    {
        if (ctx.services)
        {
            if (auto* renderService = ctx.services->tryGet<vultra::IRenderService>())
                renderService->releaseOverrideRenderWorld(&m_ModelPreviewWorld);
        }
        m_ModelPreviewWorld.clear();
        m_ModelPreviewRoot = entt::null;
        m_ModelPreviewContentRoot = entt::null;
        m_ModelPreviewKey = "source:" + path.lexically_normal().generic_string();
        m_ModelPreviewPath = path.lexically_normal();
        m_ModelPreviewRotation = glm::quat {1.0f, 0.0f, 0.0f, 0.0f};
        m_ModelPreviewArcballVector = glm::vec3 {0.0f, 0.0f, 1.0f};
        m_ModelPreviewArcballActive = false;
        m_ModelPreviewDistanceScale = 1.0f;

        addPreviewLighting(m_ModelPreviewWorld);
        m_ModelPreviewRoot = m_ModelPreviewWorld.createEntity();
        m_ModelPreviewWorld.registry().emplace<vultra::NameComponent>(
            m_ModelPreviewRoot, vultra::NameComponent {"Preview Model Pivot"});
        m_ModelPreviewContentRoot = m_ModelPreviewWorld.createChild(m_ModelPreviewRoot);
        m_ModelPreviewWorld.registry().emplace<vultra::NameComponent>(
            m_ModelPreviewContentRoot, vultra::NameComponent {"Preview Model Content"});
        if (!ctx.services)
            return;

        auto* assetService = ctx.services->tryGet<vultra::IAssetService>();
        auto* sceneService = ctx.services->tryGet<vultra::ISceneService>();
        if (!assetService || !sceneService)
            return;

        const auto assetRoot = ctx.state.currentProject / ctx.state.currentAssetRoot;
        std::error_code ec;
        const auto rel = std::filesystem::relative(path, assetRoot, ec);
        if (ec || rel.empty())
            return;

        const auto relText = rel.generic_string();
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

    void InspectorWindow::rebuildModelPreviewWorldForMesh(EditorContext& ctx,
                                                          const vultra::CoreUUID& uuid,
                                                          const std::string& name,
                                                          const std::string& importedPath)
    {
        if (ctx.services)
        {
            if (auto* renderService = ctx.services->tryGet<vultra::IRenderService>())
                renderService->releaseOverrideRenderWorld(&m_ModelPreviewWorld);
        }
        m_ModelPreviewWorld.clear();
        m_ModelPreviewRoot = entt::null;
        m_ModelPreviewContentRoot = entt::null;
        m_ModelPreviewKey = "mesh:" + uuid.toString() + ":" + importedPath;
        m_ModelPreviewPath.clear();
        m_ModelPreviewRotation = glm::quat {1.0f, 0.0f, 0.0f, 0.0f};
        m_ModelPreviewArcballVector = glm::vec3 {0.0f, 0.0f, 1.0f};
        m_ModelPreviewArcballActive = false;
        m_ModelPreviewDistanceScale = 1.0f;

        addPreviewLighting(m_ModelPreviewWorld);
        m_ModelPreviewRoot = m_ModelPreviewWorld.createEntity();
        m_ModelPreviewWorld.registry().emplace<vultra::NameComponent>(
            m_ModelPreviewRoot, vultra::NameComponent {"Preview Mesh Pivot"});
        m_ModelPreviewContentRoot = m_ModelPreviewWorld.createChild(m_ModelPreviewRoot);
        m_ModelPreviewWorld.registry().emplace<vultra::NameComponent>(
            m_ModelPreviewContentRoot, vultra::NameComponent {"Preview Mesh Content"});
        auto entity = m_ModelPreviewWorld.createChild(m_ModelPreviewContentRoot);
        auto& reg = m_ModelPreviewWorld.registry();
        reg.emplace<vultra::NameComponent>(entity, vultra::NameComponent {name.empty() ? "Mesh Preview" : name});
        reg.emplace<vultra::MeshComponent>(entity, vultra::MeshComponent {.mesh = uuid});
        if (ctx.services)
        {
            if (auto* assetService = ctx.services->tryGet<vultra::IAssetService>())
                centerPreviewContent(m_ModelPreviewWorld, *assetService, m_ModelPreviewContentRoot);
        }
    }
} // namespace vultra_app
