#include "editor_app/ui/windows/inspector_window.hpp"

#include "common/ui_widgets.hpp"
#include "common/file_dialog.hpp"
#include "editor_app/selection.hpp"

#include <IconsMaterialDesignIcons.h>
#include <ImGuiFileDialog/ImGuiFileDialog.h>
#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/render_backend_service.hpp>
#include <vultra/function/services/render_service.hpp>
#include <vultra/function/services/world_service.hpp>
#include <vultra/function/world/components/camera_component.hpp>
#include <vultra/function/world/components/entity_status_component.hpp>
#include <vultra/function/world/components/gaussian_splat_component.hpp>
#include <vultra/function/world/components/hierarchy_component.hpp>
#include <vultra/function/world/components/id_component.hpp>
#include <vultra/function/world/components/light_component.hpp>
#include <vultra/function/world/components/mesh_component.hpp>
#include <vultra/function/world/components/name_component.hpp>
#include <vultra/function/world/components/prefab_instance_component.hpp>
#include <vultra/function/world/components/script_component.hpp>
#include <vultra/function/world/components/transform_component.hpp>
#include <vultra/function/world/world.hpp>

#include <entt/meta/meta.hpp>
#include <glm/common.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace vultra_app
{
    namespace
    {
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

        bool drawTransformComponent(vultra::TransformComponent& transform)
        {
            if (!ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
                return false;

            bool changed = false;
            ImGui::Indent();

            changed |= drawVec3Control("Position", transform.position, glm::vec3 {0.0f}, 0.05f);

            glm::vec3 rotationDeg = glm::degrees(glm::eulerAngles(transform.rotation));
            if (drawVec3Control("Rotation", rotationDeg, glm::vec3 {0.0f}, 0.5f))
            {
                transform.rotation = glm::quat(glm::radians(rotationDeg));
                changed            = true;
            }

            changed |= drawVec3Control("Scale", transform.scale, glm::vec3 {1.0f}, 0.05f);

            ImGui::Unindent();

            if (changed)
                transform.dirty = true;
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
            if (is("clearColor"))
                return "clearColor";
            if (is("priority"))
                return "priority";
            if (is("rendererKey"))
                return "rendererKey";
            if (is("kind"))
                return "kind";
            if (is("color"))
                return "color";
            if (is("intensity"))
                return "intensity";
            if (is("direction"))
                return "direction";
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
            return vasset::VAssetType::eUnknown;
        }

        const char* expectedAssetLabelForField(const char* fieldName)
        {
            const auto type = expectedAssetTypeForField(fieldName);
            if (type == vasset::VAssetType::eMesh)
                return "Mesh";
            if (type == vasset::VAssetType::eGaussianSplat)
                return "Gaussian Splat";
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
                    if (!entry.sourcePath.empty())
                    {
                        ImGui::SameLine();
                        ImGui::TextDisabled("%s", entry.sourcePath.c_str());
                    }
                }
                if (!any)
                    ImGui::TextDisabled("No imported assets of this type.");
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
            const auto text = uuid.valid() ? uuid.toString()
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
                    const bool disabled = key == "universal_rt" && !rayTracingAvailable;
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
        bool drawMetaFields(EditorContext* ctx,
                            Component&     component,
                            const std::function<void(const char*)>& onChanged = {})
        {
            bool changedAny = false;
            auto instance   = entt::forward_as_meta(component);
            auto meta       = entt::resolve<Component>();
            for (auto [fieldId, field] : meta.data())
            {
                const char* rawName = field.name() ? field.name() : metaFieldNameFromId(fieldId);
                if (rawName == nullptr)
                    continue;

                auto value = field.get(instance);
                if (!value)
                    continue;

                const auto label = displayFieldName(rawName);
                if (drawMetaValue(ctx, field, value, rawName, label.c_str()))
                {
                    field.set(instance, value);
                    changedAny = true;
                    if (onChanged)
                        onChanged(rawName);
                }
            }
            return changedAny;
        }

        template<typename Component>
        bool componentHeader(vultra::World& world, entt::entity entity)
        {
            if (!world.registry().all_of<Component>(entity))
                return false;
            const char* metaName = entt::resolve<Component>().name();
            const char* label    = (metaName != nullptr) ? displayComponentName(metaName) : componentDisplayName<Component>();
            if (std::strcmp(label, "Component") == 0)
                label = componentDisplayName<Component>();
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
            const char* label {};
            bool (*has)(entt::registry&, entt::entity) {};
            void (*add)(entt::registry&, entt::entity) {};
        };

        template<typename Component>
        AddComponentDescriptor addComponentDescriptor(const char* label)
        {
            return AddComponentDescriptor {
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
                addComponentDescriptor<vultra::TransformComponent>("Transform"),
                addComponentDescriptor<vultra::CameraComponent>("Camera"),
                addComponentDescriptor<vultra::LightComponent>("Light"),
                addComponentDescriptor<vultra::MeshComponent>("Mesh"),
                addComponentDescriptor<vultra::GaussianSplatComponent>("Gaussian Splat"),
                addComponentDescriptor<vultra::ScriptComponent>("Script"),
            };
            return descriptors;
        }
    } // namespace

    InspectorWindow::InspectorWindow() : EditorWindow("Inspector", ICON_MDI_TUNE) {}

    void InspectorWindow::onClosed(EditorContext& ctx) { m_PreviewCache.clear(ctx); }

    void InspectorWindow::onDestroy(EditorContext& ctx) { m_PreviewCache.clear(ctx); }

    void InspectorWindow::draw(EditorContext& ctx)
    {
        ImGui::Begin(title().c_str(), &m_Open);

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

        if (auto* transform = reg.try_get<vultra::TransformComponent>(e))
        {
            if (drawTransformComponent(*transform))
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

        drawReflectedComponent<vultra::MeshComponent>(
            world, e, &ctx, [&](vultra::MeshComponent&, const char*) { ctx.state.sceneDirty = true; });
        drawReflectedComponent<vultra::GaussianSplatComponent>(
            world, e, &ctx, [&](vultra::GaussianSplatComponent&, const char*) { ctx.state.sceneDirty = true; });
        drawReflectedComponent<vultra::LightComponent>(
            world, e, &ctx, [&](vultra::LightComponent&, const char*) { ctx.state.sceneDirty = true; });

        if (componentHeader<vultra::CameraComponent>(world, e))
        {
            auto& camera = reg.get<vultra::CameraComponent>(e);
            if (ImGui::Button(ICON_MDI_CAMERA_SWITCH "  Align With Scene View", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
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
                camera,
                [&](const char* fieldName)
                {
                    if (std::strcmp(fieldName, "primary") == 0 && camera.primary)
                    {
                        auto view = reg.view<vultra::CameraComponent>();
                        for (auto other : view)
                        {
                            if (other != e)
                                view.get<vultra::CameraComponent>(other).primary = false;
                        }
                    }
                    if (camera.zFar <= camera.zNear)
                        camera.zFar = camera.zNear + 0.001f;
                    ctx.state.sceneDirty = true;
                });
        }

        drawReflectedComponent<vultra::ScriptComponent>(
            world, e, &ctx, [&](vultra::ScriptComponent&, const char*) { ctx.state.sceneDirty = true; });

        if (componentHeader<vultra::PrefabInstanceComponent>(world, e))
        {
            auto& prefab = reg.get<vultra::PrefabInstanceComponent>(e);
            ImGui::TextWrapped("URI: %s", prefab.prefabUri.c_str());
            if (prefab.prefabId.valid())
                ImGui::TextWrapped("UUID: %s", prefab.prefabId.toString().c_str());
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
} // namespace vultra_app
