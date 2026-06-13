#include "editor_app/ui/windows/inspector_window.hpp"

#include "common/file_dialog.hpp"
#include "common/ui_widgets.hpp"
#include "editor_app/editor_app.hpp"
#include "editor_app/editor_history.hpp"
#include "editor_app/selection.hpp"
#include "editor_app/ui/settings_widgets.hpp"

#include <IconsMaterialDesignIcons.h>
#include <ImGuiFileDialog/ImGuiFileDialog.h>
#include <vultra/core/i18n/i18n.hpp>
#include <vultra/function/asset/builtin_assets.hpp>
#include <vultra/function/imgui/imgui_dpi.hpp>
#include <vultra/function/material/material_asset.hpp>
#include <vultra/function/material_graph/material_graph.hpp>
#include <vultra/function/material_graph/material_node_registry.hpp>
#include <vultra/function/rendering/render_structs.hpp>
#include <vultra/function/services/animation_service.hpp>
#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/camera_service.hpp>
#include <vultra/function/services/imgui_service.hpp>
#include <vultra/function/services/render_backend_service.hpp>
#include <vultra/function/services/render_service.hpp>
#include <vultra/function/services/scene_service.hpp>
#include <vultra/function/services/shader_service.hpp>
#include <vultra/function/services/world_service.hpp>
#include <vultra/function/world/components/animator_component.hpp>
#include <vultra/function/world/components/audio_listener_component.hpp>
#include <vultra/function/world/components/audio_source_component.hpp>
#include <vultra/function/world/components/box_shape_component.hpp>
#include <vultra/function/world/components/camera_component.hpp>
#include <vultra/function/world/components/capsule_shape_component.hpp>
#include <vultra/function/world/components/entity_status_component.hpp>
#include <vultra/function/world/components/environment_component.hpp>
#include <vultra/function/world/components/gaussian_splat_component.hpp>
#include <vultra/function/world/components/hierarchy_component.hpp>
#include <vultra/function/world/components/id_component.hpp>
#include <vultra/function/world/components/layer_component.hpp>
#include <vultra/function/world/components/light_component.hpp>
#include <vultra/function/world/components/mesh_component.hpp>
#include <vultra/function/world/components/name_component.hpp>
#include <vultra/function/world/components/particle_emitter_component.hpp>
#include <vultra/function/world/components/prefab_instance_component.hpp>
#include <vultra/function/world/components/reflection_probe_component.hpp>
#include <vultra/function/world/components/rigid_body_component.hpp>
#include <vultra/function/world/components/script_component.hpp>
#include <vultra/function/world/components/sphere_shape_component.hpp>
#include <vultra/function/world/components/transform_component.hpp>
#include <vultra/function/world/components/ui_components.hpp>
#include <vultra/function/world/components/xr_view_component.hpp>
#include <vultra/function/world/world.hpp>

#include <vasset/vanimation.hpp>
#include <vasset/vmesh.hpp>
#include <vasset/mesh_import_params.hpp>
#include <vasset/vimport.hpp>

#include <entt/meta/meta.hpp>
#include <glm/common.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <sol/sol.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
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

        bool isAudioSourceAsset(const std::filesystem::path& path)
        {
            return sourceAssetHasExtension(path, {".wav", ".mp3", ".flac", ".ogg"});
        }

        std::string sourcePrefixBeforeSubAsset(const std::string& sourcePath)
        {
            const auto hash = sourcePath.find('#');
            return hash == std::string::npos ? sourcePath : sourcePath.substr(0, hash);
        }

        vultra::CoreUUID coreUuidFromRegistryKey(const std::string& uuid)
        {
            vbase::UUID parsed {};
            return vbase::try_parse_uuid(uuid.c_str(), parsed) ? vultra::CoreUUID(parsed) : vultra::CoreUUID {};
        }

        void applyPreviewAnimatorControls(vultra::World& world, bool playing, bool loop, float speed)
        {
            auto& reg  = world.registry();
            auto  view = reg.view<vultra::AnimatorComponent>();
            for (auto entity : view)
            {
                auto& animator = view.get<vultra::AnimatorComponent>(entity);
                animator.playOnStart = false;
                animator.playing     = playing;
                animator.loop        = loop;
                animator.speed       = speed;
            }
        }

        void resetPreviewAnimators(vultra::World& world)
        {
            auto& reg  = world.registry();
            auto  view = reg.view<vultra::AnimatorComponent>();
            for (auto entity : view)
                view.get<vultra::AnimatorComponent>(entity).time = 0.0f;
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

        bool isMaterialAssetSource(const std::filesystem::path& path)
        {
            auto name = path.filename().generic_string();
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return name.ends_with(".vmat.json");
        }

        bool isMaterialGraphNodeAssetSource(const std::filesystem::path& path)
        {
            auto name = path.filename().generic_string();
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return name.ends_with(".vmatnode.json");
        }

        bool readJsonFile(const std::filesystem::path& path, nlohmann::json& out, std::string& error)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file)
            {
                error = "Failed to open file.";
                return false;
            }

            try
            {
                file >> out;
            }
            catch (const std::exception& e)
            {
                error = e.what();
                return false;
            }
            return true;
        }

        bool writeJsonFile(const std::filesystem::path& path, const nlohmann::json& doc, std::string& error)
        {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file)
            {
                error = "Failed to open file for writing.";
                return false;
            }

            file << doc.dump(2) << '\n';
            if (!file)
            {
                error = "Failed to write file.";
                return false;
            }
            return true;
        }

        vultra::material::MaterialSourceRef materialSourceFromJson(const nlohmann::json& doc)
        {
            auto parsed = vultra::material::materialAssetFromJson(doc);
            return parsed.asset.source;
        }

        void materialSourceToJson(const vultra::material::MaterialSourceRef& source, nlohmann::json& doc)
        {
            auto sourceJson = nlohmann::json::object();
            sourceJson["kind"] = vultra::material::materialSourceKindToString(source.kind);
            if (!source.id.empty())
                sourceJson["id"] = source.id;
            if (!source.uri.empty())
                sourceJson["uri"] = source.uri;
            if (!source.shaderLibrary.empty())
                sourceJson["shaderLibrary"] = source.shaderLibrary;
            doc["source"] = std::move(sourceJson);
        }

        std::string stringFromJsonProperty(const nlohmann::json& properties, const std::string& name)
        {
            if (!properties.contains(name) || !properties[name].is_string())
                return {};
            return properties[name].get<std::string>();
        }

        glm::vec4 vec4FromJsonProperty(const nlohmann::json& properties,
                                       const vultra::material::MaterialPropertySchema& param)
        {
            glm::vec4 fallback {1.0f};
            if (const auto* value = std::get_if<glm::vec4>(&param.defaultValue))
                fallback = *value;

            if (!properties.contains(param.name) || !properties[param.name].is_array() ||
                properties[param.name].size() != 4)
            {
                return fallback;
            }

            glm::vec4 out = fallback;
            for (size_t i = 0; i < 4; ++i)
            {
                if (!properties[param.name][i].is_number())
                    return fallback;
                out[static_cast<glm::length_t>(i)] = properties[param.name][i].get<float>();
            }
            return out;
        }

        glm::vec3 vec3FromJsonProperty(const nlohmann::json& properties,
                                       const vultra::material::MaterialPropertySchema& param)
        {
            glm::vec3 fallback {0.0f};
            if (const auto* value = std::get_if<glm::vec3>(&param.defaultValue))
                fallback = *value;

            if (!properties.contains(param.name) || !properties[param.name].is_array() ||
                properties[param.name].size() != 3)
            {
                return fallback;
            }

            glm::vec3 out = fallback;
            for (size_t i = 0; i < 3; ++i)
            {
                if (!properties[param.name][i].is_number())
                    return fallback;
                out[static_cast<glm::length_t>(i)] = properties[param.name][i].get<float>();
            }
            return out;
        }

        glm::vec2 vec2FromJsonProperty(const nlohmann::json& properties,
                                       const vultra::material::MaterialPropertySchema& param)
        {
            glm::vec2 fallback {0.0f};
            if (const auto* value = std::get_if<glm::vec2>(&param.defaultValue))
                fallback = *value;

            if (!properties.contains(param.name) || !properties[param.name].is_array() ||
                properties[param.name].size() != 2)
            {
                return fallback;
            }

            glm::vec2 out = fallback;
            for (size_t i = 0; i < 2; ++i)
            {
                if (!properties[param.name][i].is_number())
                    return fallback;
                out[static_cast<glm::length_t>(i)] = properties[param.name][i].get<float>();
            }
            return out;
        }

        nlohmann::json jsonFromVec4(const glm::vec4& value)
        {
            return nlohmann::json::array({value.x, value.y, value.z, value.w});
        }

        nlohmann::json jsonFromVec3(const glm::vec3& value)
        {
            return nlohmann::json::array({value.x, value.y, value.z});
        }

        nlohmann::json jsonFromVec2(const glm::vec2& value)
        {
            return nlohmann::json::array({value.x, value.y});
        }

        float floatFromJsonProperty(const nlohmann::json& properties,
                                    const vultra::material::MaterialPropertySchema& param)
        {
            const auto fallback = std::get_if<float>(&param.defaultValue) ? std::get<float>(param.defaultValue) : 0.0f;
            if (!properties.contains(param.name) || !properties[param.name].is_number())
                return fallback;
            return properties[param.name].get<float>();
        }

        bool boolFromJsonProperty(const nlohmann::json& properties,
                                  const vultra::material::MaterialPropertySchema& param)
        {
            const auto fallback = std::get_if<bool>(&param.defaultValue) ? std::get<bool>(param.defaultValue) : false;
            if (!properties.contains(param.name) || !properties[param.name].is_boolean())
                return fallback;
            return properties[param.name].get<bool>();
        }

        int32_t intFromJsonProperty(const nlohmann::json& properties,
                                    const vultra::material::MaterialPropertySchema& param)
        {
            const auto fallback = std::get_if<int32_t>(&param.defaultValue) ? std::get<int32_t>(param.defaultValue) : 0;
            if (!properties.contains(param.name) || !properties[param.name].is_number_integer())
                return fallback;
            return properties[param.name].get<int32_t>();
        }

        vultra::material::MaterialSourceSchema resolveMaterialSchemaForEditor(EditorContext& ctx,
                                                                              const vultra::material::MaterialSourceRef& source)
        {
            if (source.kind == vultra::material::MaterialSourceKind::eShader)
            {
                if (source.id.empty() || !ctx.services)
                    return {};

                auto* shaders = ctx.services->tryGet<vultra::IShaderService>();
                if (!shaders)
                    return {};

                vultra::rhi::ShaderLibraryRuntime* library = nullptr;
                if (source.shaderLibrary == "builtin")
                {
                    library = &shaders->builtinLibrary();
                }
                else
                {
                    library = shaders->findProjectLibrary("res://shaders/project.vshaderlib.lua");
                    if (!library)
                        library = shaders->loadProjectLibrary("res://shaders/project.vshaderlib.lua");
                }
                if (!library)
                    return {};

                const auto variantHash = vultra::rhi::ShaderLibraryRuntime::computeVariantHash(
                    source.id, vshadersystem::ShaderStage::eFrag, {});
                if (!library->hasVariant(variantHash, vshadersystem::ShaderStage::eFrag))
                    return {};

                auto shader = library->load(variantHash, vshadersystem::ShaderStage::eFrag);
                return shader ? vultra::material::materialSourceSchemaFromShaderMaterialDescription(shader->materialDesc) :
                                vultra::material::MaterialSourceSchema {};
            }

            if (source.kind != vultra::material::MaterialSourceKind::eGraph || source.uri.empty())
                return vultra::material::resolveMaterialSourceSchema(source);

            auto* assets = ctx.services ? ctx.services->tryGet<vultra::IAssetService>() : nullptr;
            if (!assets)
                return {};

            auto text = assets->loadTextAssetSync(source.uri);
            if (!text)
                return {};

            auto graph = vultra::material_graph::loadGraphFromText(text.value());
            if (!graph)
                return {};

            return vultra::material::materialSourceSchemaFromGraph(*graph);
        }

        bool drawShaderLibrarySelector(const char* label, std::array<char, 128>& value);
        bool drawLibraryShaderSelector(EditorContext&          ctx,
                                       const char*             label,
                                       const std::string&      stage,
                                       std::array<char, 128>&  library,
                                       std::array<char, 128>&  shader,
                                       bool                    allowEmpty);

        bool drawMaterialStringInput(const char* label, std::string& value)
        {
            std::array<char, 512> buffer {};
            copyName(buffer, value);
            // A "##"-prefixed label means the caller (e.g. a table cell) already drew the
            // label and wants only the control; otherwise lay it out as a property row.
            const bool hidden  = label != nullptr && label[0] == '#' && label[1] == '#';
            bool       changed = false;
            if (hidden)
            {
                changed = ImGui::InputText(label, buffer.data(), buffer.size());
            }
            else
            {
                ui::beginPropertyRow(label);
                changed = ImGui::InputText("##value", buffer.data(), buffer.size());
                ui::endPropertyRow();
            }
            if (!changed)
                return false;
            value = buffer.data();
            return true;
        }

        constexpr const char* kMaterialGraphValueTypeLabels[] = {
            "Bool",
            "Int",
            "Float",
            "Vec2",
            "Vec3",
            "Vec4",
            "Color",
            "Texture2D",
        };

        vultra::material_graph::ValueType materialGraphValueTypeFromIndex(const int index)
        {
            using enum vultra::material_graph::ValueType;
            switch (index)
            {
                case 0:
                    return eBool;
                case 1:
                    return eInt;
                case 2:
                    return eFloat;
                case 3:
                    return eVec2;
                case 4:
                    return eVec3;
                case 5:
                    return eVec4;
                case 6:
                    return eColor;
                case 7:
                    return eTexture2D;
                default:
                    return eFloat;
            }
        }

        int materialGraphValueTypeIndex(const vultra::material_graph::ValueType type)
        {
            using enum vultra::material_graph::ValueType;
            switch (type)
            {
                case eBool:
                    return 0;
                case eInt:
                    return 1;
                case eFloat:
                    return 2;
                case eVec2:
                    return 3;
                case eVec3:
                    return 4;
                case eVec4:
                    return 5;
                case eColor:
                    return 6;
                case eTexture2D:
                    return 7;
                default:
                    return 2;
            }
        }

        nlohmann::json defaultMaterialGraphValue(const vultra::material_graph::ValueType type)
        {
            using enum vultra::material_graph::ValueType;
            switch (type)
            {
                case eBool:
                    return false;
                case eInt:
                    return 0;
                case eVec2:
                    return nlohmann::json::array({0.0f, 0.0f});
                case eVec3:
                    return nlohmann::json::array({0.0f, 0.0f, 0.0f});
                case eVec4:
                    return nlohmann::json::array({0.0f, 0.0f, 0.0f, 0.0f});
                case eColor:
                    return nlohmann::json::array({1.0f, 1.0f, 1.0f, 1.0f});
                case eTexture2D:
                    return std::string {};
                case eFloat:
                default:
                    return 0.0f;
            }
        }

        bool drawJsonDefaultValue(nlohmann::json& value, const vultra::material_graph::ValueType type)
        {
            using enum vultra::material_graph::ValueType;

            // Texture default draws its own property row via drawMaterialStringInput.
            if (type == eTexture2D)
            {
                std::string v = value.is_string() ? value.get<std::string>() : std::string {};
                if (!drawMaterialStringInput(vultra::tr("inspector.graphNode.defaultUri"), v))
                    return false;
                value = v;
                return true;
            }

            ui::beginPropertyRow(vultra::tr("common.default"));
            bool changed = false;
            switch (type)
            {
                case eBool: {
                    bool v = value.is_boolean() ? value.get<bool>() : false;
                    if (ImGui::Checkbox("##value", &v))
                    {
                        value   = v;
                        changed = true;
                    }
                    break;
                }
                case eInt: {
                    int v = value.is_number_integer() ? value.get<int>() : 0;
                    if (ImGui::InputInt("##value", &v))
                    {
                        value   = v;
                        changed = true;
                    }
                    break;
                }
                case eVec2: {
                    glm::vec2 v {0.0f};
                    if (value.is_array() && value.size() >= 2)
                        v = {value[0].get<float>(), value[1].get<float>()};
                    if (ImGui::DragFloat2("##value", &v.x, 0.01f))
                    {
                        value   = nlohmann::json::array({v.x, v.y});
                        changed = true;
                    }
                    break;
                }
                case eVec3: {
                    glm::vec3 v {0.0f};
                    if (value.is_array() && value.size() >= 3)
                        v = {value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
                    if (ImGui::DragFloat3("##value", &v.x, 0.01f))
                    {
                        value   = nlohmann::json::array({v.x, v.y, v.z});
                        changed = true;
                    }
                    break;
                }
                case eVec4:
                case eColor: {
                    glm::vec4 v = type == eColor ? glm::vec4 {1.0f} : glm::vec4 {0.0f};
                    if (value.is_array() && value.size() >= 4)
                        v = {value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>()};
                    const bool edited = type == eColor ? ImGui::ColorEdit4("##value", &v.x) :
                                                         ImGui::DragFloat4("##value", &v.x, 0.01f);
                    if (edited)
                    {
                        value   = nlohmann::json::array({v.x, v.y, v.z, v.w});
                        changed = true;
                    }
                    break;
                }
                case eFloat:
                default: {
                    float v = value.is_number() ? value.get<float>() : 0.0f;
                    if (ImGui::DragFloat("##value", &v, 0.01f))
                    {
                        value   = v;
                        changed = true;
                    }
                    break;
                }
            }
            ui::endPropertyRow();
            return changed;
        }

        void drawImagePreviewPlaceholder(const std::filesystem::path& path, const char* note)
        {
            ImGui::TextUnformatted(vultra::tr("inspector.preview"));
            const float  size = std::min(ImGui::GetContentRegionAvail().x, vultra::ui::dp(220.0f));
            const ImVec2 pos  = ImGui::GetCursorScreenPos();
            ImDrawList*  dl   = ImGui::GetWindowDrawList();
            const ImVec2 max {pos.x + size, pos.y + size};

            const float tile = vultra::ui::dp(16.0f);
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
            dl->AddRect(pos, max, IM_COL32(150, 160, 175, 255), vultra::ui::dp(6.0f), 0, vultra::ui::dp(1.5f));
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

        std::filesystem::path textureImportSidecarPath(const std::filesystem::path& path)
        {
            auto sidecar = path;
            sidecar.replace_extension(".vimport");
            return sidecar;
        }

        std::string relativeTextureSourcePath(const EditorContext& ctx, const std::filesystem::path& path)
        {
            const auto      assetRoot = (ctx.state.currentProject / ctx.state.currentAssetRoot).lexically_normal();
            std::error_code ec;
            auto            rel = std::filesystem::relative(path.lexically_normal(), assetRoot, ec);
            return ec || rel.empty() ? path.filename().generic_string() : rel.generic_string();
        }

        std::string importedTexturePathForSource(EditorContext& ctx, const std::string& relativeSource)
        {
            if (auto* assets = ctx.services ? ctx.services->tryGet<vultra::IAssetService>() : nullptr)
            {
                for (const auto& [_, entry] : assets->registry().getRegistry())
                {
                    if (entry.type == vasset::VAssetType::eTexture && entry.sourcePath == relativeSource &&
                        !entry.importedPath.empty())
                    {
                        return entry.importedPath;
                    }
                }
            }

            auto stem = std::filesystem::path(relativeSource).stem().generic_string();
            if (stem.empty())
                stem = "texture";
            return (std::filesystem::path("imported") / vasset::toString(vasset::VAssetType::eTexture) / stem)
                .generic_string();
        }

        std::filesystem::path importedTexturePhysicalPath(EditorContext& ctx, const std::filesystem::path& sourcePath)
        {
            const auto relativeSource = relativeTextureSourcePath(ctx, sourcePath);
            auto       sidecar        = textureImportSidecarPath(sourcePath);
            std::string importedPath;
            if (auto loaded = vasset::loadVImport(sidecar.generic_string()))
                importedPath = loaded.value().output;
            if (importedPath.empty())
                importedPath = importedTexturePathForSource(ctx, relativeSource);
            return (ctx.state.currentProject / ctx.state.currentAssetRoot / std::filesystem::path(importedPath))
                .lexically_normal();
        }

        std::string formatFileSizeIfPresent(const std::filesystem::path& path, const char* missingLabel)
        {
            std::error_code ec;
            if (!std::filesystem::is_regular_file(path, ec) || ec)
                return missingLabel;
            const auto size = std::filesystem::file_size(path, ec);
            return ec ? missingLabel : formatFileSize(size);
        }

        bool textureImportParamsEqual(const vasset::TextureImportParams& lhs, const vasset::TextureImportParams& rhs)
        {
            const auto& a = lhs.options;
            const auto& b = rhs.options;
            return lhs.subtype == rhs.subtype && a.generateMipmaps == b.generateMipmaps && a.flipY == b.flipY &&
                   a.targetTextureFileFormat == b.targetTextureFileFormat && a.uastc == b.uastc &&
                   a.qualityLevel == b.qualityLevel && a.compressionLevel == b.compressionLevel &&
                   a.compressOnlyLargeTextures == b.compressOnlyLargeTextures &&
                   a.downscaleLargeTextures == b.downscaleLargeTextures &&
                   a.downscaleMinDimension == b.downscaleMinDimension &&
                   a.downscaleTargetDimension == b.downscaleTargetDimension &&
                   a.bakeNormalMap == b.bakeNormalMap && a.directXNormalMap == b.directXNormalMap;
        }

        const char* textureSubtypeLabel(std::string_view subtype)
        {
            if (subtype == vasset::kTextureSubtypeUiSprite)
                return vultra::tr("inspector.texture.subtype.uiSprite");
            if (subtype == vasset::kTextureSubtypeNormalMap)
                return vultra::tr("inspector.texture.subtype.normalMap");
            if (subtype == vasset::kTextureSubtypeCursor)
                return vultra::tr("inspector.texture.subtype.cursor");
            if (subtype == vasset::kTextureSubtypeDefault)
                return vultra::tr("common.default");
            return subtype.empty() ? vultra::tr("common.default") : subtype.data();
        }

        bool drawTextureFileFormatCombo(vasset::VTextureFileFormat& format)
        {
            struct Option
            {
                const char* label;
                vasset::VTextureFileFormat value;
            };
            constexpr Option kOptions[] = {
                {"KTX2", vasset::VTextureFileFormat::eKTX2},
                {"PNG", vasset::VTextureFileFormat::ePNG},
                {"JPEG", vasset::VTextureFileFormat::eJPEG},
                {"TGA", vasset::VTextureFileFormat::eTGA},
                {"BMP", vasset::VTextureFileFormat::eBMP},
                {"HDR", vasset::VTextureFileFormat::eHDR},
                {"EXR", vasset::VTextureFileFormat::eEXR},
            };

            const char* preview = "KTX2";
            for (const auto& option : kOptions)
                if (option.value == format)
                    preview = option.label;

            bool changed = false;
            ui::beginPropertyRow(vultra::tr("inspector.texture.targetFormat"));
            if (ImGui::BeginCombo("##TargetFormat", preview))
            {
                for (const auto& option : kOptions)
                {
                    const bool selected = option.value == format;
                    if (ImGui::Selectable(option.label, selected))
                    {
                        format  = option.value;
                        changed = true;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ui::endPropertyRow();
            return changed;
        }

        void queueTextureImport(EditorContext& ctx, const std::filesystem::path& path, const bool forceReimport)
        {
            const auto normalized = path.lexically_normal();
            if (std::find(ctx.state.pendingAssetImportPaths.begin(),
                          ctx.state.pendingAssetImportPaths.end(),
                          normalized) == ctx.state.pendingAssetImportPaths.end())
            {
                ctx.state.pendingAssetImportPaths.push_back(normalized);
            }
            ctx.state.pendingAssetImportRefresh = true;
            ctx.state.pendingAssetImportForceReimport |= forceReimport;
        }

        const char* displayComponentName(const char* metaName)
        {
            if (std::strcmp(metaName, "TransformComponent") == 0)
                return vultra::tr("inspector.component.transform");
            if (std::strcmp(metaName, "RectTransformComponent") == 0)
                return vultra::tr("inspector.component.rectTransform");
            if (std::strcmp(metaName, "CanvasComponent") == 0)
                return vultra::tr("inspector.component.canvas");
            if (std::strcmp(metaName, "UiPanelComponent") == 0)
                return vultra::tr("inspector.component.uiPanel");
            if (std::strcmp(metaName, "UiImageComponent") == 0)
                return vultra::tr("inspector.component.uiImage");
            if (std::strcmp(metaName, "UiTextComponent") == 0)
                return vultra::tr("inspector.component.uiText");
            if (std::strcmp(metaName, "UiButtonComponent") == 0)
                return vultra::tr("inspector.component.uiButton");
            if (std::strcmp(metaName, "UiToggleComponent") == 0)
                return vultra::tr("inspector.component.uiToggle");
            if (std::strcmp(metaName, "UiSliderComponent") == 0)
                return vultra::tr("inspector.component.uiSlider");
            if (std::strcmp(metaName, "UiProgressBarComponent") == 0)
                return vultra::tr("inspector.component.uiProgressBar");
            if (std::strcmp(metaName, "UiLayoutComponent") == 0)
                return vultra::tr("inspector.component.uiLayout");
            if (std::strcmp(metaName, "EntityStatusComponent") == 0)
                return vultra::tr("inspector.component.status");
            if (std::strcmp(metaName, "MeshComponent") == 0)
                return vultra::tr("inspector.component.mesh");
            if (std::strcmp(metaName, "AnimatorComponent") == 0)
                return vultra::tr("inspector.component.animator");
            if (std::strcmp(metaName, "GaussianSplatComponent") == 0)
                return vultra::tr("inspector.component.gaussianSplat");
            if (std::strcmp(metaName, "CameraComponent") == 0)
                return vultra::tr("inspector.component.camera");
            if (std::strcmp(metaName, "XRViewComponent") == 0)
                return vultra::tr("inspector.component.xrView");
            if (std::strcmp(metaName, "EnvironmentComponent") == 0)
                return vultra::tr("inspector.component.environment");
            if (std::strcmp(metaName, "ReflectionProbeComponent") == 0)
                return vultra::tr("inspector.component.reflectionProbe");
            if (std::strcmp(metaName, "LightComponent") == 0)
                return vultra::tr("inspector.component.light");
            if (std::strcmp(metaName, "ParticleEmitterComponent") == 0)
                return vultra::tr("inspector.component.particleEmitter");
            if (std::strcmp(metaName, "RigidBodyComponent") == 0)
                return vultra::tr("inspector.component.rigidBody");
            if (std::strcmp(metaName, "BoxShapeComponent") == 0)
                return vultra::tr("inspector.component.boxShape");
            if (std::strcmp(metaName, "SphereShapeComponent") == 0)
                return vultra::tr("inspector.component.sphereShape");
            if (std::strcmp(metaName, "CapsuleShapeComponent") == 0)
                return vultra::tr("inspector.component.capsuleShape");
            if (std::strcmp(metaName, "ScriptComponent") == 0)
                return vultra::tr("inspector.component.script");
            if (std::strcmp(metaName, "AudioSourceComponent") == 0)
                return vultra::tr("inspector.component.audioSource");
            if (std::strcmp(metaName, "AudioListenerComponent") == 0)
                return vultra::tr("inspector.component.audioListener");
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
            return vultra::tr("inspector.component.transform");
        }

        template<>
        const char* componentDisplayName<vultra::RectTransformComponent>()
        {
            return vultra::tr("inspector.component.rectTransform");
        }

        template<>
        const char* componentDisplayName<vultra::CanvasComponent>()
        {
            return vultra::tr("inspector.component.canvas");
        }

        template<>
        const char* componentDisplayName<vultra::UiPanelComponent>()
        {
            return vultra::tr("inspector.component.uiPanel");
        }

        template<>
        const char* componentDisplayName<vultra::UiImageComponent>()
        {
            return vultra::tr("inspector.component.uiImage");
        }

        template<>
        const char* componentDisplayName<vultra::UiTextComponent>()
        {
            return vultra::tr("inspector.component.uiText");
        }

        template<>
        const char* componentDisplayName<vultra::UiButtonComponent>()
        {
            return vultra::tr("inspector.component.uiButton");
        }

        template<>
        const char* componentDisplayName<vultra::UiToggleComponent>()
        {
            return vultra::tr("inspector.component.uiToggle");
        }

        template<>
        const char* componentDisplayName<vultra::UiSliderComponent>()
        {
            return vultra::tr("inspector.component.uiSlider");
        }

        template<>
        const char* componentDisplayName<vultra::UiProgressBarComponent>()
        {
            return vultra::tr("inspector.component.uiProgressBar");
        }

        template<>
        const char* componentDisplayName<vultra::UiLayoutComponent>()
        {
            return vultra::tr("inspector.component.uiLayout");
        }

        template<>
        const char* componentDisplayName<vultra::HierarchyComponent>()
        {
            return vultra::tr("inspector.component.hierarchy");
        }

        template<>
        const char* componentDisplayName<vultra::MeshComponent>()
        {
            return vultra::tr("inspector.component.mesh");
        }

        template<>
        const char* componentDisplayName<vultra::AnimatorComponent>()
        {
            return vultra::tr("inspector.component.animator");
        }

        template<>
        const char* componentDisplayName<vultra::GaussianSplatComponent>()
        {
            return vultra::tr("inspector.component.gaussianSplat");
        }

        template<>
        const char* componentDisplayName<vultra::CameraComponent>()
        {
            return vultra::tr("inspector.component.camera");
        }

        template<>
        const char* componentDisplayName<vultra::XRViewComponent>()
        {
            return vultra::tr("inspector.component.xrView");
        }

        template<>
        const char* componentDisplayName<vultra::EnvironmentComponent>()
        {
            return vultra::tr("inspector.component.environment");
        }

        template<>
        const char* componentDisplayName<vultra::ReflectionProbeComponent>()
        {
            return vultra::tr("inspector.component.reflectionProbe");
        }

        template<>
        const char* componentDisplayName<vultra::LightComponent>()
        {
            return vultra::tr("inspector.component.light");
        }

        template<>
        const char* componentDisplayName<vultra::RigidBodyComponent>()
        {
            return vultra::tr("inspector.component.rigidBody");
        }

        template<>
        const char* componentDisplayName<vultra::BoxShapeComponent>()
        {
            return vultra::tr("inspector.component.boxShape");
        }

        template<>
        const char* componentDisplayName<vultra::SphereShapeComponent>()
        {
            return vultra::tr("inspector.component.sphereShape");
        }

        template<>
        const char* componentDisplayName<vultra::CapsuleShapeComponent>()
        {
            return vultra::tr("inspector.component.capsuleShape");
        }

        template<>
        const char* componentDisplayName<vultra::ScriptComponent>()
        {
            return vultra::tr("inspector.component.script");
        }

        template<>
        const char* componentDisplayName<vultra::PrefabInstanceComponent>()
        {
            return vultra::tr("inspector.component.prefab");
        }

        std::string displayFieldName(const char* raw)
        {
            if (raw == nullptr)
                return {};

            // i18n: reflected component fields are labelled by their raw member name. Prefer a
            // translation keyed by that name (inspector.field.<rawName>); fall back to a prettified
            // camelCase split when the active catalog has no entry (tr() returns the key on a miss).
            std::string key = "inspector.field.";
            key += raw;
            if (const char* translated = vultra::tr(key); std::strcmp(translated, key.c_str()) != 0)
                return translated;

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
            // Explicit per-axis hover/active palette (RGB-coded XYZ).
            const ui::VectorAxisSpec axes[] = {
                {"X", &value.x, resetValue.x, {0.55f, 0.16f, 0.18f, 1.0f}, {0.75f, 0.22f, 0.24f, 1.0f},
                 {0.9f, 0.28f, 0.32f, 1.0f}},
                {"Y", &value.y, resetValue.y, {0.20f, 0.46f, 0.20f, 1.0f}, {0.28f, 0.64f, 0.28f, 1.0f},
                 {0.34f, 0.78f, 0.34f, 1.0f}},
                {"Z", &value.z, resetValue.z, {0.16f, 0.28f, 0.58f, 1.0f}, {0.22f, 0.38f, 0.78f, 1.0f},
                 {0.30f, 0.48f, 0.94f, 1.0f}},
            };
            return ui::drawVectorControl(label, axes, speed, 42.0f);
        }

        bool drawVec2Control(const char* label, glm::vec2& value, const glm::vec2& resetValue, const float speed = 0.05f)
        {
            // Hover/active derived from the base color (+0.12 / +0.20), matching the original vec2 styling.
            const auto             lighten = [](ImVec4 c, float d) { return ImVec4 {c.x + d, c.y + d, c.z + d, 1.0f}; };
            const ImVec4           cx {0.55f, 0.16f, 0.18f, 1.0f};
            const ImVec4           cy {0.20f, 0.46f, 0.20f, 1.0f};
            const ui::VectorAxisSpec axes[] = {
                {"X", &value.x, resetValue.x, cx, lighten(cx, 0.12f), lighten(cx, 0.20f)},
                {"Y", &value.y, resetValue.y, cy, lighten(cy, 0.12f), lighten(cy, 0.20f)},
            };
            return ui::drawVectorControl(label, axes, speed, 48.0f);
        }

        bool drawAnchorPresetPreview(vultra::RectTransformComponent& rect)
        {
            bool changed = false;
            ImGui::PushID("AnchorPresetPreview");
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const float  cell   = vultra::ui::dp(18.0f);
            const float  gap    = vultra::ui::dp(3.0f);
            auto*        dl     = ImGui::GetWindowDrawList();
            const ImVec2 boxMin = origin;
            const ImVec2 boxMax {origin.x + cell * 3.0f + gap * 2.0f, origin.y + cell * 3.0f + gap * 2.0f};
            dl->AddRectFilled(boxMin, boxMax, IM_COL32(18, 22, 28, 255), vultra::ui::dp(4.0f));
            dl->AddRect(boxMin, boxMax, IM_COL32(255, 255, 255, 32), vultra::ui::dp(4.0f));

            const glm::vec2 anchorCenter = (rect.anchorMin + rect.anchorMax) * 0.5f;
            const int selectedX = std::clamp(static_cast<int>(std::round(anchorCenter.x * 2.0f)), 0, 2);
            const int selectedY = std::clamp(static_cast<int>(std::round(anchorCenter.y * 2.0f)), 0, 2);

            for (int y = 0; y < 3; ++y)
            {
                for (int x = 0; x < 3; ++x)
                {
                    const ImVec2 p {origin.x + static_cast<float>(x) * (cell + gap),
                                    origin.y + static_cast<float>(y) * (cell + gap)};
                    ImGui::SetCursorScreenPos(p);
                    const bool selected = x == selectedX && y == selectedY;
                    if (selected)
                    {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4 {0.95f, 0.58f, 0.16f, 1.0f});
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4 {1.0f, 0.68f, 0.22f, 1.0f});
                    }
                    if (ImGui::Button(("##preset" + std::to_string(x) + std::to_string(y)).c_str(), ImVec2 {cell, cell}))
                    {
                        const glm::vec2 anchor {static_cast<float>(x) * 0.5f, static_cast<float>(y) * 0.5f};
                        rect.anchorMin = anchor;
                        rect.anchorMax = anchor;
                        rect.pivot     = anchor;
                        changed        = true;
                    }
                    if (selected)
                        ImGui::PopStyleColor(2);
                }
            }
            ImGui::SetCursorScreenPos(ImVec2 {origin.x, boxMax.y + ImGui::GetStyle().ItemSpacing.y});
            ImGui::PopID();
            return changed;
        }

        bool drawRectTransformComponentFields(vultra::RectTransformComponent& rect)
        {
            bool changed = false;
            ImGui::PushID("RectTransformCustom");

            ImGui::TextDisabled("%s", vultra::tr("inspector.rectTransform.anchors"));
            ImGui::SameLine(vultra::ui::dp(92.0f));
            changed |= drawAnchorPresetPreview(rect);

            changed |= drawVec2Control(vultra::tr("common.position"), rect.anchoredPositionPx, glm::vec2 {0.0f}, 0.5f);
            changed |= drawVec2Control(vultra::tr("inspector.rectTransform.size"), rect.sizeDeltaPx, glm::vec2 {0.0f}, 0.5f);
            changed |= drawVec2Control(vultra::tr("inspector.rectTransform.anchorMin"), rect.anchorMin, glm::vec2 {0.5f}, 0.01f);
            changed |= drawVec2Control(vultra::tr("inspector.rectTransform.anchorMax"), rect.anchorMax, glm::vec2 {0.5f}, 0.01f);
            changed |= drawVec2Control(vultra::tr("inspector.rectTransform.pivot"), rect.pivot, glm::vec2 {0.5f}, 0.01f);
            ui::beginPropertyRow(vultra::tr("common.rotation"), vultra::ui::dp(92.0f));
            changed |= ImGui::DragFloat("##Rotation", &rect.rotation, 0.5f, 0.0f, 0.0f, "%.2f deg");
            ui::endPropertyRow();
            changed |= drawVec2Control(vultra::tr("common.scale"), rect.scale, glm::vec2 {1.0f}, 0.01f);

            rect.anchorMin = glm::clamp(rect.anchorMin, glm::vec2 {0.0f}, glm::vec2 {1.0f});
            rect.anchorMax = glm::clamp(rect.anchorMax, glm::vec2 {0.0f}, glm::vec2 {1.0f});
            rect.pivot     = glm::clamp(rect.pivot, glm::vec2 {0.0f}, glm::vec2 {1.0f});
            rect.scale     = glm::max(rect.scale, glm::vec2 {0.05f});

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

            changed |= drawVec3Control(vultra::tr("common.position"), transform.position, glm::vec3 {0.0f}, 0.05f);

            if (light && (light->kind == 0 || light->kind == 2))
            {
                glm::vec3 direction = directionFromTransform(transform);
                if (drawVec3Control(vultra::tr("inspector.transform.direction"), direction, glm::vec3 {0.0f, -1.0f, 0.0f}, 0.01f))
                {
                    transform.rotation = rotationFromDirection(direction);
                    changed            = true;
                }
            }
            else
            {
                if (drawQuaternionDeltaControl(vultra::tr("common.rotation"), transform.rotation, entityId))
                    changed = true;
            }

            changed |= drawVec3Control(vultra::tr("common.scale"), transform.scale, glm::vec3 {1.0f}, 0.05f);

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
            bool       changed     = false;
            const std::string kindLabels = std::string {vultra::tr("inspector.light.kind.directional")} + '\0' +
                                           vultra::tr("inspector.light.kind.point") + '\0' +
                                           vultra::tr("inspector.light.kind.spot") + '\0' +
                                           vultra::tr("inspector.light.kind.rectangleArea") + '\0';
            int kindIndex = static_cast<int>(std::min(light.kind, 3u));
            ui::beginPropertyRow(vultra::tr("inspector.light.kindLabel"));
            if (ImGui::Combo("##Kind", &kindIndex, kindLabels.c_str()))
            {
                light.kind = static_cast<uint32_t>(std::clamp(kindIndex, 0, 3));
                changed    = true;
            }
            ui::endPropertyRow();

            ui::beginPropertyRow(vultra::tr("inspector.light.color"));
            changed |= ImGui::ColorEdit3("##Color", &light.color.x);
            ui::endPropertyRow();
            ui::beginPropertyRow(vultra::tr("inspector.light.intensity"));
            changed |= ImGui::DragFloat("##Intensity", &light.intensity, 0.05f, 0.0f, 10000.0f, "%.2f");
            ui::endPropertyRow();

            if (light.kind == 1 || light.kind == 2)
            {
                ui::beginPropertyRow(vultra::tr("inspector.light.range"));
                changed |= ImGui::DragFloat("##Range", &light.range, 0.05f, 0.0f, 1000.0f, "%.2f");
                ui::endPropertyRow();
                ui::beginPropertyRow(vultra::tr("inspector.light.radius"));
                changed |= ImGui::DragFloat("##Radius", &light.radius, 0.01f, 0.0f, 100.0f, "%.3f");
                ui::endPropertyRow();
            }
            if (light.kind == 2)
            {
                ui::beginPropertyRow(vultra::tr("inspector.light.innerCone"));
                changed |= ImGui::DragFloat("##InnerCone", &light.innerConeDegrees, 0.25f, 0.0f, 179.0f, "%.1f");
                ui::endPropertyRow();
                ui::beginPropertyRow(vultra::tr("inspector.light.outerCone"));
                changed |= ImGui::DragFloat("##OuterCone", &light.outerConeDegrees, 0.25f, 0.0f, 179.0f, "%.1f");
                ui::endPropertyRow();
                light.outerConeDegrees = std::max(light.outerConeDegrees, light.innerConeDegrees);
            }
            if (light.kind == 3)
            {
                ui::beginPropertyRow(vultra::tr("inspector.light.width"));
                changed |= ImGui::DragFloat("##Width", &light.width, 0.05f, 0.0f, 100.0f, "%.2f");
                ui::endPropertyRow();
                ui::beginPropertyRow(vultra::tr("inspector.light.height"));
                changed |= ImGui::DragFloat("##Height", &light.height, 0.05f, 0.0f, 100.0f, "%.2f");
                ui::endPropertyRow();
            }
            ui::beginPropertyRow(vultra::tr("inspector.light.castsShadow"));
            changed |= ImGui::Checkbox("##CastsShadow", &light.castsShadow);
            ui::endPropertyRow();
            ui::beginPropertyRow(vultra::tr("inspector.light.twoSided"));
            changed |= ImGui::Checkbox("##TwoSided", &light.twoSided);
            ui::endPropertyRow();
            return changed;
        }

        bool drawXRViewComponentFields(EditorContext& ctx, vultra::XRViewComponent& xrView)
        {
            bool changed = false;

            ui::beginPropertyRow(vultra::tr("common.enabled"));
            changed |= ImGui::Checkbox("##Enabled", &xrView.enabled);
            ui::endPropertyRow();

            int               trackingOrigin = static_cast<int>(xrView.trackingOrigin);
            const std::string trackingOrigins =
                std::string {vultra::tr("inspector.xrView.trackingOrigin.local")} + '\0' +
                vultra::tr("inspector.xrView.trackingOrigin.stage") + '\0';
            ui::beginPropertyRow(vultra::tr("inspector.xrView.trackingOriginLabel"));
            if (ImGui::Combo("##TrackingOrigin", &trackingOrigin, trackingOrigins.c_str()))
            {
                xrView.trackingOrigin = static_cast<uint32_t>(std::clamp(trackingOrigin, 0, 1));
                changed               = true;
            }
            ui::endPropertyRow();

            int               stereoGraphMode = static_cast<int>(xrView.stereoGraphMode);
            const std::string stereoGraphModes =
                std::string {vultra::tr("inspector.xrView.stereoGraph.singleGraphStereo")} + '\0';
            ui::beginPropertyRow(vultra::tr("inspector.xrView.stereoGraphLabel"));
            if (ImGui::Combo("##StereoGraph", &stereoGraphMode, stereoGraphModes.c_str()))
            {
                xrView.stereoGraphMode = 0u;
                changed                = true;
            }
            ui::endPropertyRow();

            ui::beginPropertyRow(vultra::tr("inspector.xrView.fallbackMono"));
            changed |= ImGui::Checkbox("##FallbackMono", &xrView.fallbackMono);
            ui::endPropertyRow();

            if (auto* backend = ctx.services ? ctx.services->tryGet<vultra::IRenderBackendService>() : nullptr)
            {
                ImGui::SeparatorText(vultra::tr("inspector.xrView.runtime"));
                ImGui::TextUnformatted(
                    vultra::trf("inspector.xrView.openxr",
                                backend->isXREnabled() ? vultra::tr("common.enabled") : vultra::tr("common.disabled"))
                        .c_str());
                ImGui::TextUnformatted(
                    vultra::trf("inspector.xrView.mirror",
                                backend->isXRMirrorEnabled() ? vultra::tr("common.enabled") :
                                                               vultra::tr("common.disabled"))
                        .c_str());
                ImGui::BeginDisabled(!xrView.enabled);
                if (ImGui::SmallButton((std::string {ICON_MDI_HEADSET "  "} + vultra::tr("inspector.xrView.requestSession")).c_str()))
                {
                    backend->requestXRSession(false);
                    backend->requestXRSession(true);
                }
                ImGui::EndDisabled();
                if (!backend->isXREnabled())
                    ImGui::TextDisabled("%s", vultra::tr("inspector.xrView.sessionHint"));
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
                ctx.state.statusMessage = vultra::tr("inspector.camera.sceneViewUnavailable");
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
                camera.fovY = ctx.state.sceneCamera.fovY;
            ctx.state.statusMessage = vultra::tr("inspector.camera.alignedToSceneView");
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
            ctx.state.sceneCameraAlignRequest.fovY =
                camera.projection == 0u ? camera.fovY : ctx.state.sceneCamera.fovY;
            ctx.state.statusMessage = vultra::tr("inspector.camera.sceneViewAligned");
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
            if (is("mask"))
                return "mask";
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
            if (is("fovY"))
                return "fovY";
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
            if (is("cullingMask"))
                return "cullingMask";
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
            if (is("sortOrder"))
                return "sortOrder";
            if (is("referenceResolutionPx"))
                return "referenceResolutionPx";
            if (is("scaleMode"))
                return "scaleMode";
            if (is("anchorMin"))
                return "anchorMin";
            if (is("anchorMax"))
                return "anchorMax";
            if (is("pivot"))
                return "pivot";
            if (is("anchoredPositionPx"))
                return "anchoredPositionPx";
            if (is("sizeDeltaPx"))
                return "sizeDeltaPx";
            if (is("rotation"))
                return "rotation";
            if (is("borderRadiusPx"))
                return "borderRadiusPx";
            if (is("texture"))
                return "texture";
            if (is("tint"))
                return "tint";
            if (is("fitMode"))
                return "fitMode";
            if (is("text"))
                return "text";
            if (is("fontSizePx"))
                return "fontSizePx";
            if (is("horizontalAlign"))
                return "horizontalAlign";
            if (is("verticalAlign"))
                return "verticalAlign";
            if (is("interactable"))
                return "interactable";
            if (is("targetGraphic"))
                return "targetGraphic";
            if (is("normalColor"))
                return "normalColor";
            if (is("hoveredColor"))
                return "hoveredColor";
            if (is("pressedColor"))
                return "pressedColor";
            if (is("paddingPx"))
                return "paddingPx";
            if (is("marginPx"))
                return "marginPx";
            if (is("spacingPx"))
                return "spacingPx";
            if (is("cellSizePx"))
                return "cellSizePx";

            // ParticleEmitterComponent
            if (is("playing"))
                return "playing";
            if (is("worldSpace"))
                return "worldSpace";
            if (is("maxParticles"))
                return "maxParticles";
            if (is("emissionRate"))
                return "emissionRate";
            if (is("lifetime"))
                return "lifetime";
            if (is("lifetimeVariance"))
                return "lifetimeVariance";
            if (is("spawnRadius"))
                return "spawnRadius";
            if (is("startVelocity"))
                return "startVelocity";
            if (is("velocityVariance"))
                return "velocityVariance";
            if (is("gravity"))
                return "gravity";
            if (is("startSize"))
                return "startSize";
            if (is("endSize"))
                return "endSize";
            if (is("startColor"))
                return "startColor";
            if (is("endColor"))
                return "endColor";

            // AudioSourceComponent (playing/loop/playOnStart shared above) / AudioListenerComponent
            if (is("clip"))
                return "clip";
            if (is("volume"))
                return "volume";
            if (is("pitch"))
                return "pitch";
            if (is("loop"))
                return "loop";
            if (is("playOnStart"))
                return "playOnStart";
            if (is("spatial"))
                return "spatial";
            if (is("minDistance"))
                return "minDistance";
            if (is("maxDistance"))
                return "maxDistance";
            if (is("rolloff"))
                return "rolloff";

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
            if (std::strcmp(fieldName, "texture") == 0)
                return vasset::VAssetType::eTexture;
            if (std::strcmp(fieldName, "clip") == 0)
                return vasset::VAssetType::eAudio;
            return vasset::VAssetType::eUnknown;
        }

        const char* expectedAssetLabelForField(const char* fieldName)
        {
            const auto type = expectedAssetTypeForField(fieldName);
            if (type == vasset::VAssetType::eMesh)
                return vultra::tr("inspector.assetType.mesh");
            if (type == vasset::VAssetType::eSkeleton)
                return vultra::tr("inspector.assetType.skeleton");
            if (type == vasset::VAssetType::eAnimation)
                return vultra::tr("inspector.assetType.animation");
            if (type == vasset::VAssetType::eGaussianSplat)
                return vultra::tr("inspector.assetType.gaussianSplat");
            if (type == vasset::VAssetType::eTexture)
                return vultra::tr("inspector.assetType.texture");
            if (type == vasset::VAssetType::eAudio)
                return vultra::tr("inspector.assetType.audio");
            return vultra::tr("inspector.assetType.asset");
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

        std::string materialForkFileName(const uint32_t slot)
        {
            return "forked_builtin_material_slot_" + std::to_string(slot) + ".vmat.json";
        }

        std::filesystem::path uniqueMaterialAssetPath(const std::filesystem::path& materialDir, const uint32_t slot)
        {
            auto target = (materialDir / materialForkFileName(slot)).lexically_normal();
            if (!std::filesystem::exists(target))
                return target;

            for (uint32_t i = 2; i < 1000; ++i)
            {
                target = (materialDir / ("forked_builtin_material_slot_" + std::to_string(slot) + "_" +
                                         std::to_string(i) + ".vmat.json"))
                             .lexically_normal();
                if (!std::filesystem::exists(target))
                    return target;
            }
            return {};
        }

        bool forkBuiltinDefaultMaterial(EditorContext& ctx, const uint32_t slot, std::string& outUri)
        {
            const auto assetRoot = editorAssetRoot(ctx);
            if (assetRoot.empty())
            {
                ctx.state.statusMessage = vultra::tr("inspector.forkMaterial.noAssetRoot");
                return false;
            }

            const auto materialDir = (assetRoot / "materials").lexically_normal();
            std::error_code ec;
            std::filesystem::create_directories(materialDir, ec);
            if (ec)
            {
                ctx.state.statusMessage = vultra::trf("inspector.forkMaterial.failed", ec.message());
                return false;
            }

            const auto target = uniqueMaterialAssetPath(materialDir, slot);
            if (target.empty())
            {
                ctx.state.statusMessage = vultra::tr("inspector.forkMaterial.noFileName");
                return false;
            }

            nlohmann::json doc {
                {"type", "Material"},
                {"version", 1},
                {"name", "Forked Builtin Material"},
                {"source", {{"kind", "builtin"}, {"id", "builtin/pbr"}}},
                {"properties",
                 {
                     {"baseColor", {1.0f, 1.0f, 1.0f, 1.0f}},
                     {"metallic", 0.0f},
                     {"roughness", 0.5f},
                     {"baseColorTexture", ""},
                 }},
            };

            std::string error;
            if (!writeJsonFile(target, doc, error))
            {
                ctx.state.statusMessage = vultra::trf("inspector.forkMaterial.failed", error);
                return false;
            }

            auto uri = pathToResUri(ctx, target);
            if (uri.empty())
            {
                ctx.state.statusMessage = vultra::tr("inspector.forkMaterial.outsideAssets");
                return false;
            }

            bool registered = false;
            if (ctx.services)
            {
                if (auto* assets = ctx.services->tryGet<vultra::IAssetService>())
                {
                    assets->clearTextAssetOverride(uri);
                    registered = assets->reimportAsset(uri, false);
                }
            }

            ++ctx.state.assetFileGeneration;
            outUri                  = std::move(uri);
            ctx.state.statusMessage = registered ? vultra::trf("inspector.forkMaterial.forked", outUri) :
                                                   vultra::tr("inspector.forkMaterial.forkedNoImport");
            return true;
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
            ui::beginPropertyRow(label);
            const char* preview = value[0] == '\0' ? vultra::tr("inspector.noneAngle") : value.data();
            if (ImGui::BeginCombo("##shader", preview))
            {
                if (allowEmpty && ImGui::Selectable(vultra::tr("inspector.noneAngle"), value[0] == '\0'))
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
            ui::endPropertyRow();
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
            ImGui::SameLine(vultra::ui::dp(160.0f));
            ImGui::SetNextItemWidth(vultra::ui::dp(280.0f));
            const char* preview = value[0] == '\0' ? vultra::tr("inspector.noneAngle") : value.data();
            if (ImGui::BeginCombo("##shader", preview))
            {
                if (allowEmpty && ImGui::Selectable(vultra::tr("inspector.noneAngle"), value[0] == '\0'))
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
            ImGui::SetNextItemWidth(vultra::ui::dp(220.0f));
            changed |= ImGui::InputText("##manual", value.data(), value.size());
            ImGui::PopID();
            return changed;
        }

        bool drawShaderLibrarySelector(const char* label, std::array<char, 128>& value)
        {
            // Library is a closed choice ("project" or "builtin"); a combo covers every
            // valid value, so there is no manual text entry. collectShaderIdsForLibrary
            // treats any non-"builtin" value as "project".
            bool changed = false;
            std::array options {"project", "builtin"};

            ui::beginPropertyRow(label);
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
            ui::endPropertyRow();
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
            ImGui::TextUnformatted(vultra::tr("inspector.shader.vertex"));
            ImGui::SameLine(vultra::ui::dp(160.0f));
            ImGui::SetNextItemWidth(vultra::ui::dp(280.0f));
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
                ImGui::TextUnformatted(vultra::trf("inspector.assetPicker.select", vasset::toString(expectedType)).c_str());
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
                    ImGui::TextDisabled("%s", vultra::tr("inspector.assetPicker.noAssets"));
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
                               std::string {ICON_MDI_BULLSEYE "  "} +
                                   vultra::trf("inspector.assetField.none", expectedAssetLabelForField(fieldName));
            const auto dialogKey = std::string("InspectorSelectAsset_") + fieldName;

            ImGui::TextUnformatted(label);
            ImGui::PushID(label);
            const float buttonSize = ImGui::GetFrameHeight();
            const float spacing    = ImGui::GetStyle().ItemSpacing.x;
            const float fieldWidth =
                std::max(1.0f, ImGui::GetContentRegionAvail().x - buttonSize * 2.0f - spacing * 2.0f);
            ImGui::Button(text.c_str(), ImVec2(fieldWidth, 0.0f));
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", vultra::trf("inspector.assetField.dropTip", expectedAssetLabelForField(fieldName)).c_str());

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
                            ctx->state.statusMessage = vultra::tr("inspector.assetField.dropRejected");
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_MDI_BULLSEYE) && ctx)
                ImGui::OpenPopup(dialogKey.c_str());
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", vultra::trf("inspector.assetField.selectTip", expectedAssetLabelForField(fieldName)).c_str());

            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_MDI_CLOSE_CIRCLE_OUTLINE) && uuid.valid())
            {
                uuid    = {};
                changed = true;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", vultra::tr("inspector.assetField.clearTip"));

            changed |= drawAssetRegistryPicker(ctx, dialogKey.c_str(), expectedAssetTypeForField(fieldName), uuid);
            ImGui::PopID();

            return changed;
        }

        // The skeleton bundled inside an entity's (or a descendant's) skinned mesh asset.
        vultra::CoreUUID meshBundledSkeleton(EditorContext& ctx, entt::registry& reg, entt::entity e)
        {
            if (!ctx.services)
                return {};
            auto* assets = ctx.services->tryGet<vultra::IAssetService>();
            if (!assets)
                return {};
            const auto search = [&](auto&& self, entt::entity cursor) -> vultra::CoreUUID {
                if (cursor == entt::null || !reg.valid(cursor))
                    return {};
                if (const auto* mesh = reg.try_get<vultra::MeshComponent>(cursor); mesh && mesh->mesh.valid())
                {
                    auto        handle = assets->loadMeshSync(mesh->mesh);
                    const auto* cpu    = handle.cpu();
                    if (cpu && cpu->hasSkin && vultra::CoreUUID {cpu->skeleton}.valid())
                        return vultra::CoreUUID {cpu->skeleton};
                }
                const auto* hierarchy = reg.try_get<vultra::HierarchyComponent>(cursor);
                for (auto child = hierarchy ? hierarchy->firstChild : entt::null; child != entt::null;)
                {
                    const auto* ch   = reg.try_get<vultra::HierarchyComponent>(child);
                    const auto  next = ch ? ch->nextSibling : entt::null;
                    if (auto found = self(self, child); found.valid())
                        return found;
                    child = next;
                }
                return {};
            };
            return search(search, e);
        }

        // Shared skeleton row for the animator: defaults to the mesh's bundled skeleton, optional override.
        bool drawAnimatorSkeletonRow(EditorContext& ctx, entt::registry& reg, entt::entity e, vultra::CoreUUID& skeleton)
        {
            bool       changed  = false;
            const auto fromMesh = meshBundledSkeleton(ctx, reg, e);
            if (skeleton.valid())
            {
                changed |= drawUuidObjectField(&ctx, skeleton, "skeleton", vultra::tr("inspector.animator.skeleton"));
                if (ImGui::SmallButton((std::string {ICON_MDI_CLOSE "  "} + vultra::tr("inspector.animator.clearSkeletonOverride")).c_str()))
                {
                    skeleton = {};
                    changed  = true;
                }
            }
            else
            {
                ui::beginPropertyRow(vultra::tr("inspector.animator.skeleton"));
                ImGui::TextDisabled("%s",
                                    fromMesh.valid() ?
                                        (std::string {ICON_MDI_BONE "  "} + vultra::tr("inspector.animator.fromMesh")).c_str() :
                                        (std::string {ICON_MDI_ALERT "  "} + vultra::tr("inspector.animator.noSkinnedMesh")).c_str());
                ui::endPropertyRow();
                if (ImGui::SmallButton((std::string {ICON_MDI_PENCIL "  "} + vultra::tr("inspector.animator.overrideSkeleton")).c_str()))
                {
                    skeleton = fromMesh; // seed from the mesh, then it becomes editable
                    changed  = true;
                }
            }
            return changed;
        }

        // Merged animator inspector: a mode toggle switches between single-clip and animator-graph mode.
        bool drawAnimatorComponentFields(EditorContext& ctx, entt::registry& reg, entt::entity e,
                                         vultra::AnimatorComponent& animator)
        {
            bool changed = false;

            // --- Mode selector ---
            const std::string kModes = std::string {vultra::tr("inspector.animator.mode.singleClip")} + '\0' +
                                       vultra::tr("inspector.animator.mode.graph") + '\0';
            int mode = animator.mode == 1u ? 1 : 0;
            ui::beginPropertyRow(vultra::tr("inspector.animator.modeLabel"));
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("##AnimatorMode", &mode, kModes.c_str()))
            {
                animator.mode = static_cast<uint32_t>(mode);
                changed       = true;
            }
            ui::endPropertyRow();

            ImGui::Separator();

            // --- Skeleton (shared by both modes) ---
            changed |= drawAnimatorSkeletonRow(ctx, reg, e, animator.skeleton);

            ImGui::Separator();

            if (animator.mode == 1u)
            {
                // --- Graph mode ---
                const auto lastSegment = [](const std::string& uri) {
                    const auto sl = uri.find_last_of('/');
                    return sl == std::string::npos ? uri : uri.substr(sl + 1);
                };
                ui::beginPropertyRow(vultra::tr("inspector.animator.graph"));
                const std::string preview =
                    animator.graph.empty() ? std::string {ICON_MDI_RUN_FAST "  "} + vultra::tr("inspector.noneParen") :
                                             std::string(ICON_MDI_RUN_FAST "  ") + lastSegment(animator.graph);
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::BeginCombo("##AnimatorGraphAsset", preview.c_str()))
                {
                    if (ImGui::Selectable(vultra::tr("inspector.noneParen"), animator.graph.empty()))
                    {
                        animator.graph.clear();
                        changed = true;
                    }
                    // Enumerate registered animator-graph assets (imported on startup/save), so the
                    // list is consistent regardless of the on-disk folder layout.
                    auto* assets = ctx.services ? ctx.services->tryGet<vultra::IAssetService>() : nullptr;
                    bool  any    = false;
                    if (assets)
                        for (const auto& [uuidText, entry] : assets->registry().getRegistry())
                        {
                            if (entry.type != vasset::VAssetType::eAnimatorGraphJson)
                                continue;
                            if (!isUserSelectableAssetEntry(entry))
                                continue;
                            const auto uri = entrySourceUri(entry);
                            if (uri.empty())
                                continue;
                            any              = true;
                            const auto label = assetDisplayName(entry) + "##" + uuidText;
                            if (ImGui::Selectable(label.c_str(), uri == animator.graph))
                            {
                                animator.graph = uri;
                                changed        = true;
                            }
                            ImGui::SameLine();
                            ImGui::TextDisabled("%s", uri.c_str());
                        }
                    if (!any)
                        ImGui::TextDisabled("%s", vultra::tr("inspector.animator.noGraphs"));
                    ImGui::EndCombo();
                }
                ui::endPropertyRow();

                if (!animator.graph.empty() &&
                    ImGui::Button((std::string {ICON_MDI_PENCIL "  "} + vultra::tr("inspector.animator.editGraph")).c_str(),
                                  ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
                {
                    ctx.state.currentEditingAnimatorGraph = animator.graph;
                    ctx.state.animatorGraphOpenRequested  = true;
                    ctx.state.editorWindowFocusRequested  = "Animator Graph";
                }

                ImGui::Separator();
                ui::beginPropertyRow(vultra::tr("inspector.animator.playOnStart"));
                changed |= ImGui::Checkbox("##ctrlPlayOnStart", &animator.playOnStart);
                ui::endPropertyRow();
                ui::beginPropertyRow(vultra::tr("inspector.animator.speed"));
                changed |= ImGui::DragFloat("##ctrlSpeed", &animator.speed, 0.01f, -8.0f, 8.0f, "%.3f");
                ui::endPropertyRow();
                return changed;
            }

            // --- Single-clip mode ---
            changed |= drawUuidObjectField(&ctx, animator.animation, "animation", vultra::tr("inspector.animator.animation"));

            ImGui::Separator();
            ui::beginPropertyRow(vultra::tr("inspector.animator.playOnStart"));
            changed |= ImGui::Checkbox("##PlayOnStart", &animator.playOnStart);
            ui::endPropertyRow();
            ui::beginPropertyRow(vultra::tr("inspector.animator.playing"));
            changed |= ImGui::Checkbox("##Playing", &animator.playing);
            ui::endPropertyRow();
            ui::beginPropertyRow(vultra::tr("inspector.animator.loop"));
            changed |= ImGui::Checkbox("##Loop", &animator.loop);
            ui::endPropertyRow();
            ui::beginPropertyRow(vultra::tr("inspector.animator.speed"));
            changed |= ImGui::DragFloat("##Speed", &animator.speed, 0.01f, -8.0f, 8.0f, "%.3f");
            ui::endPropertyRow();

            float time = std::max(animator.time, 0.0f);
            ui::beginPropertyRow(vultra::tr("inspector.animator.time"));
            if (ImGui::DragFloat("##Time", &time, 0.01f, 0.0f, 0.0f, "%.3f s"))
            {
                animator.time = std::max(time, 0.0f);
                changed       = true;
            }
            ui::endPropertyRow();

            if (ImGui::Button((std::string {ICON_MDI_RESTART "  "} + vultra::tr("inspector.animator.resetTime")).c_str(),
                              ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
            {
                animator.time = 0.0f;
                changed       = true;
            }
            return changed;
        }

        bool drawScriptUriObjectField(EditorContext* ctx, std::string& uri, const char* label)
        {
            bool       changed = false;
            const auto text    = uri.empty() ?
                                     std::string {ICON_MDI_BULLSEYE "  "} + vultra::tr("inspector.script.none") :
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
                ImGuiFileDialog::Instance()->OpenDialog(kScriptDialogKey, vultra::tr("inspector.script.dialogTitle"), ".lua", config);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", vultra::tr("inspector.script.chooseTip"));

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
                                ctx->state.statusMessage = vultra::tr("inspector.script.dropNotLua");
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
                    ImGuiFileDialog::Instance()->OpenDialog(kScriptDialogKey, vultra::tr("inspector.script.dialogTitle"), ".lua", config);
                }
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", vultra::tr("inspector.script.selectTip"));
            ImGui::PopID();

            ui::ScopedPopupStyle fileDialogStyle;
            if (ctx &&
                ImGuiFileDialog::Instance()->Display(kScriptDialogKey,
                                                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings,
                                                     ImVec2(vultra::ui::dp(640.0f), vultra::ui::dp(420.0f))))
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
                            ctx->state.statusMessage = vultra::tr("inspector.script.mustBeInRoot");
                        }
                    }
                }
                ImGuiFileDialog::Instance()->Close();
            }

            if (!uri.empty() &&
                ImGui::SmallButton(
                    (std::string {ICON_MDI_CLOSE "  "} + vultra::tr("inspector.clear") + "##" + label).c_str()))
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

        std::vector<std::string> collectMaterialUris(EditorContext& ctx)
        {
            std::vector<std::string> out;
            out.emplace_back(vultra::kBuiltinDefaultMaterialUri);
            const auto               root = editorAssetRoot(ctx);
            std::error_code          ec;
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
                if (!name.ends_with(".vmat.json"))
                    continue;
                const auto rel = std::filesystem::relative(path, root, ec);
                if (!ec && !rel.empty() && isImportedAssetPath(rel.generic_string()))
                    continue;
                if (auto uri = pathToResUri(ctx, path); !uri.empty())
                    out.push_back(std::move(uri));
            }
            std::sort(out.begin(), out.end());
            return out;
        }

        bool drawMaterialUriField(EditorContext* ctx, std::string& uri, const char* label)
        {
            bool changed = false;
            ImGui::TextUnformatted(label);
            ImGui::PushID(label);
            const auto preview = uri.empty() ? vultra::tr("inspector.noneAngle") : uri.c_str();
            ImGui::SetNextItemWidth(std::max(
                1.0f, ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x));
            if (ImGui::BeginCombo("##MaterialUri", preview))
            {
                if (ImGui::Selectable(vultra::tr("inspector.noneAngle"), uri.empty()))
                {
                    uri.clear();
                    changed = true;
                }
                if (ctx)
                {
                    for (const auto& candidate : collectMaterialUris(*ctx))
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

        bool drawMaterialGraphUriField(EditorContext* ctx, std::string& uri, const char* label)
        {
            bool changed = false;
            ImGui::TextUnformatted(label);
            ImGui::PushID(label);
            const auto preview = uri.empty() ? vultra::tr("inspector.noneAngle") : uri.c_str();
            ImGui::SetNextItemWidth(std::max(
                1.0f, ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x));
            if (ImGui::BeginCombo("##MaterialGraphUri", preview))
            {
                if (ImGui::Selectable(vultra::tr("inspector.noneAngle"), uri.empty()))
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

        bool isMaterialPropertyBlockSupported(const vultra::material::MaterialPropertyType type)
        {
            return type == vultra::material::MaterialPropertyType::eFloat ||
                   type == vultra::material::MaterialPropertyType::eColor ||
                   type == vultra::material::MaterialPropertyType::eVec4 ||
                   type == vultra::material::MaterialPropertyType::eTexture2D;
        }

        vultra::material::MaterialSourceSchema resolveMaterialSlotOverrideSchema(EditorContext* ctx,
                                                                                 const vultra::MaterialSlotOverride& slot)
        {
            if (!ctx || !ctx->services)
                return {};

            auto* assets = ctx->services->tryGet<vultra::IAssetService>();
            if (!assets)
                return {};

            if (!slot.material.empty())
            {
                auto text = assets->loadTextAssetSync(slot.material);
                if (!text)
                    return {};

                const auto parsed = vultra::material::loadMaterialAssetFromText(text.value());
                if (!parsed.ok())
                    return {};
                return resolveMaterialSchemaForEditor(*ctx, parsed.asset.source);
            }

            if (!slot.materialGraph.empty())
            {
                vultra::material::MaterialSourceRef source;
                source.kind = vultra::material::MaterialSourceKind::eGraph;
                source.uri  = slot.materialGraph;
                return resolveMaterialSchemaForEditor(*ctx, source);
            }

            return {};
        }

        vultra::MaterialPropertyBlockEntry
        materialPropertyBlockEntryFromSchema(const vultra::material::MaterialPropertySchema& schema)
        {
            vultra::MaterialPropertyBlockEntry entry;
            entry.name = schema.name;
            switch (schema.type)
            {
                case vultra::material::MaterialPropertyType::eTexture2D:
                    entry.type = vultra::MaterialPropertyBlockValueType::eTexture2D;
                    if (const auto* value = std::get_if<std::string>(&schema.defaultValue))
                        entry.textureUri = *value;
                    break;
                case vultra::material::MaterialPropertyType::eColor:
                case vultra::material::MaterialPropertyType::eVec4:
                    entry.type = vultra::MaterialPropertyBlockValueType::eColor;
                    if (const auto* value = std::get_if<glm::vec4>(&schema.defaultValue))
                        entry.colorValue = *value;
                    break;
                case vultra::material::MaterialPropertyType::eFloat:
                default:
                    entry.type = vultra::MaterialPropertyBlockValueType::eFloat;
                    if (const auto* value = std::get_if<float>(&schema.defaultValue))
                        entry.floatValue = *value;
                    break;
            }
            return entry;
        }

        bool drawAddMaterialPropertyBlockEntry(EditorContext* ctx, vultra::MaterialSlotOverride& slot)
        {
            auto schema = resolveMaterialSlotOverrideSchema(ctx, slot);
            std::vector<const vultra::material::MaterialPropertySchema*> candidates;
            for (const auto& param : schema.parameters)
            {
                if (!isMaterialPropertyBlockSupported(param.type))
                    continue;
                const auto exists = std::any_of(slot.properties.begin(), slot.properties.end(), [&](const auto& entry) {
                    return entry.name == param.name;
                });
                if (!exists)
                    candidates.push_back(&param);
            }

            bool changed = false;
            ImGui::BeginDisabled(candidates.empty());
            if (ImGui::BeginCombo(vultra::tr("inspector.material.addSourceProperty"),
                                  candidates.empty() ? vultra::tr("inspector.noneAngle") : vultra::tr("inspector.selectAngle")))
            {
                for (const auto* param : candidates)
                {
                    const auto label = param->displayName.empty() ? param->name : param->displayName;
                    if (ImGui::Selectable(label.c_str()))
                    {
                        slot.properties.push_back(materialPropertyBlockEntryFromSchema(*param));
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();
            return changed;
        }

        bool drawRendererKeyCombo(EditorContext* ctx, std::string& rendererKey, const char* label)
        {
            auto* renderService = ctx && ctx->services ? ctx->services->tryGet<vultra::IRenderService>() : nullptr;
            auto* renderBackend =
                ctx && ctx->services ? ctx->services->tryGet<vultra::IRenderBackendService>() : nullptr;
            auto keys = renderService ? renderService->rendererKeys() : std::vector<std::string> {};

            keys.erase(std::remove(keys.begin(), keys.end(), "editor-shell"), keys.end());
            keys.erase(std::remove(keys.begin(), keys.end(), "editor-ui2d"), keys.end());

            if (rendererKey.empty())
                rendererKey = "universal";

            if (std::find(keys.begin(), keys.end(), rendererKey) == keys.end())
                keys.push_back(rendererKey);

            std::sort(keys.begin(), keys.end());
            keys.erase(std::unique(keys.begin(), keys.end()), keys.end());

            bool        changed = false;
            const char* preview = rendererKey.empty() ? vultra::tr("inspector.noneAngle") : rendererKey.c_str();
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
                            ImGui::SetTooltip("%s", vultra::tr("inspector.rendererKey.rayTracingUnavailable"));
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            return changed;
        }

        bool drawRenderLayerMaskField(const char* label, uint32_t& mask)
        {
            bool changed = false;
            ImGui::TextUnformatted(label);
            ImGui::PushID(label);
            const auto drawBit = [&](const char* bitLabel, const uint32_t bit) {
                bool enabled = (mask & bit) != 0u;
                if (ImGui::Checkbox(bitLabel, &enabled))
                {
                    if (enabled)
                        mask |= bit;
                    else
                        mask &= ~bit;
                    changed = true;
                }
            };
            drawBit(vultra::tr("common.default"), vultra::kRenderLayerDefaultMask);
            ImGui::SameLine();
            drawBit("UI", vultra::kRenderLayerUiMask);
            ImGui::SameLine();
            if (ImGui::SmallButton(vultra::tr("inspector.layerMask.all")))
            {
                mask    = vultra::kRenderLayerAllMask;
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(vultra::tr("inspector.clear")))
            {
                mask    = 0u;
                changed = true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("0x%08X", mask);
            ImGui::PopID();
            return changed;
        }

        // Field-name -> ordered i18n keys for the simple "uint32 enum -> Combo" inspector fields. Replaces a
        // long if-chain of near-identical Combo blocks; the special enums (render-layer mask, and
        // builtinGeometry's offset mapping) stay inline in drawMetaValue.
        const std::unordered_map<std::string_view, std::vector<const char*>>& enumFieldTable()
        {
            static const std::unordered_map<std::string_view, std::vector<const char*>> table = {
                {"projection", {"inspector.enum.projection.perspective", "inspector.enum.projection.orthographic"}},
                {"clearMode", {"inspector.enum.clearMode.color", "inspector.enum.clearMode.skybox"}},
                {"kind",
                 {"inspector.light.kind.directional", "inspector.light.kind.point", "inspector.light.kind.spot",
                  "inspector.light.kind.rectangleArea"}},
                {"motionType",
                 {"inspector.enum.motionType.static", "inspector.enum.motionType.kinematic",
                  "inspector.enum.motionType.dynamic"}},
                {"objectLayer", {"inspector.enum.objectLayer.nonMoving", "inspector.enum.objectLayer.moving"}},
                {"motionQuality", {"inspector.enum.motionQuality.discrete", "inspector.enum.motionQuality.linearCast"}},
                {"scaleMode", {"inspector.enum.scaleMode.constantPixels", "inspector.enum.scaleMode.scaleWithScreen"}},
                {"fitMode",
                 {"inspector.enum.fitMode.stretch", "inspector.enum.fitMode.contain", "inspector.enum.fitMode.cover"}},
                {"horizontalAlign",
                 {"inspector.enum.hAlign.left", "inspector.enum.hAlign.center", "inspector.enum.hAlign.right"}},
                {"verticalAlign",
                 {"inspector.enum.vAlign.top", "inspector.enum.vAlign.middle", "inspector.enum.vAlign.bottom"}},
                {"shape", {"inspector.enum.shape.box", "inspector.enum.shape.sphere"}},
            };
            return table;
        }

        // Build the '\0'-joined Combo item list from localized keys (rebuilt each frame so language switches
        // take effect), select by clamped current value, and write back the chosen index.
        bool drawEnumCombo(const char* label, uint32_t& v, const std::vector<const char*>& keys)
        {
            std::string labels;
            for (const char* key : keys)
            {
                labels += std::string {vultra::tr(key)};
                labels.push_back('\0');
            }
            int index = static_cast<int>(std::min(v, static_cast<uint32_t>(keys.size() - 1)));
            if (ImGui::Combo(label, &index, labels.c_str()))
            {
                v = static_cast<uint32_t>(index);
                return true;
            }
            return false;
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
                if (std::strcmp(fieldName, "mask") == 0 || std::strcmp(fieldName, "cullingMask") == 0)
                    return drawRenderLayerMaskField(label, *v);

                if (std::strcmp(fieldName, "builtinGeometry") == 0)
                {
                    const std::string geometryLabels =
                        std::string {vultra::tr("inspector.enum.geometry.externalMesh")} + '\0' +
                        vultra::tr("inspector.enum.geometry.quad") + '\0' + vultra::tr("inspector.enum.geometry.cube") +
                        '\0' + vultra::tr("inspector.enum.geometry.sphere") + '\0' +
                        vultra::tr("inspector.enum.geometry.capsule") + '\0';
                    int geometryIndex = *v == UINT32_MAX ? 0 : static_cast<int>(std::min(*v + 1u, 4u));
                    if (ImGui::Combo(label, &geometryIndex, geometryLabels.c_str()))
                    {
                        *v      = geometryIndex == 0 ? UINT32_MAX : static_cast<uint32_t>(geometryIndex - 1);
                        changed = true;
                    }
                    return changed;
                }

                if (const auto it = enumFieldTable().find(fieldName); it != enumFieldTable().end())
                    return drawEnumCombo(label, *v, it->second);

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

            if (auto* v = value.try_cast<glm::vec2>())
                return ImGui::DragFloat2(label, &v->x, 0.05f);

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
            // Label is already drawn by the surrounding property row; show only the type.
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("<%s>", typeName ? typeName : "unregistered");
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
                choices.push_back({.slot = 0u, .label = vultra::tr("inspector.materialSlot.slot0Default")});
                return choices;
            }

            auto* assets = ctx->services->tryGet<vultra::IAssetService>();
            if (!assets)
            {
                choices.push_back({.slot = 0u, .label = vultra::tr("inspector.materialSlot.slot0Default")});
                return choices;
            }

            const auto  handle  = assets->loadMeshAsync(mesh.mesh);
            const auto* cpuMesh = handle.cpu();
            if (!cpuMesh)
            {
                choices.push_back({.slot = 0u, .label = vultra::tr("inspector.materialSlot.slot0Loading")});
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
                    materialName = vultra::trf("inspector.materialSlot.materialN", slot);

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
                choice.label  = vultra::trf("inspector.materialSlot.slotLabel", slot, materialName);
                choice.detail =
                    subMeshName.empty() ? std::string {} : vultra::trf("inspector.materialSlot.usedBySubmesh", subMeshName);
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
                                            vultra::trf("inspector.materialSlot.slotUnknown", slot);

            bool changed = false;
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo(vultra::tr("inspector.materialSlot.slot"), selectedLabel.c_str()))
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
                    const std::string unknown = vultra::trf("inspector.materialSlot.keepSlot", slot);
                    if (ImGui::Selectable(unknown.c_str(), true))
                        changed = false;
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", vultra::tr("inspector.materialSlot.tip"));
            return changed;
        }

        // Self-laid-out reflected fields draw their own label plus a multi-line picker /
        // control grid, so they must NOT be wrapped in a single label-left/control-right
        // property row (see ai/knowledge/editor-ui-style.md). Everything else is a single
        // row and goes through beginPropertyRow with a hidden control label.
        bool metaFieldDrawsOwnRow(entt::meta_any& value, const char* fieldName)
        {
            if (value.try_cast<vultra::CoreUUID>() != nullptr)
                return true;
            if (value.try_cast<std::string>() != nullptr && fieldName != nullptr &&
                std::strcmp(fieldName, "scriptUri") == 0)
                return true;
            if (value.try_cast<uint32_t>() != nullptr && fieldName != nullptr &&
                (std::strcmp(fieldName, "mask") == 0 || std::strcmp(fieldName, "cullingMask") == 0))
                return true;
            return false;
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

                const auto label  = displayFieldName(rawName);
                const bool ownRow = metaFieldDrawsOwnRow(value, rawName);
                ImGui::PushID(static_cast<int>(fieldId));
                if (!ownRow)
                    ui::beginPropertyRow(label.c_str());
                const bool fieldChanged = drawMetaValue(
                    ctx, textureSelector, meshSelector, field, value, rawName, ownRow ? label.c_str() : "##v");
                if (!ownRow)
                    ui::endPropertyRow();
                if (fieldChanged)
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
            if (ImGui::CollapsingHeader(vultra::tr("inspector.material.overrides"), ImGuiTreeNodeFlags_DefaultOpen))
            {
                int removeIndex = -1;
                for (int i = 0; i < static_cast<int>(mesh.materialOverrides.size()); ++i)
                {
                    auto& override = mesh.materialOverrides[static_cast<size_t>(i)];
                    ImGui::PushID(i);
                    if (drawMaterialSlotCombo(ctx, mesh, override.slot))
                        changed = true;
                    if (drawMaterialUriField(ctx, override.material, vultra::tr("inspector.material.material")))
                        changed = true;
                    const bool canForkBuiltin =
                        ctx && (override.material.empty() || override.material == vultra::kBuiltinDefaultMaterialUri);
                    ImGui::BeginDisabled(!canForkBuiltin);
                    if (ImGui::SmallButton((std::string {ICON_MDI_CONTENT_COPY " "} + vultra::tr("inspector.material.forkBuiltin")).c_str()))
                    {
                        std::string forkedUri;
                        if (forkBuiltinDefaultMaterial(*ctx, override.slot, forkedUri))
                        {
                            override.material = std::move(forkedUri);
                            override.materialGraph.clear();
                            changed = true;
                        }
                    }
                    ImGui::EndDisabled();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        ImGui::SetTooltip("%s", vultra::tr("inspector.material.forkBuiltinTip"));
                    if (drawMaterialGraphUriField(ctx, override.materialGraph, vultra::tr("inspector.material.graph")))
                        changed = true;
                    if (ImGui::TreeNode(vultra::tr("inspector.material.propertyBlock")))
                    {
                        if (drawAddMaterialPropertyBlockEntry(ctx, override))
                            changed = true;
                        int removeProperty = -1;
                        for (int propertyIndex = 0; propertyIndex < static_cast<int>(override.properties.size());
                             ++propertyIndex)
                        {
                            auto& property = override.properties[static_cast<size_t>(propertyIndex)];
                            ImGui::PushID(propertyIndex);
                            if (drawMaterialStringInput(vultra::tr("common.name"), property.name))
                                changed = true;

                            int typeIndex = property.type == vultra::MaterialPropertyBlockValueType::eColor ? 1 :
                                            property.type == vultra::MaterialPropertyBlockValueType::eTexture2D ? 2 :
                                                                                                                 0;
                            const std::string typeLabels =
                                std::string {vultra::tr("inspector.material.propType.float")} + '\0' +
                                vultra::tr("inspector.material.propType.color") + '\0' +
                                vultra::tr("inspector.material.propType.texture2D") + '\0';
                            ui::beginPropertyRow(vultra::tr("common.type"));
                            const bool propTypeChanged = ImGui::Combo("##Type", &typeIndex, typeLabels.c_str());
                            ui::endPropertyRow();
                            if (propTypeChanged)
                            {
                                property.type = typeIndex == 1 ? vultra::MaterialPropertyBlockValueType::eColor :
                                                typeIndex == 2 ? vultra::MaterialPropertyBlockValueType::eTexture2D :
                                                                 vultra::MaterialPropertyBlockValueType::eFloat;
                                changed = true;
                            }

                            switch (property.type)
                            {
                                case vultra::MaterialPropertyBlockValueType::eColor:
                                    ui::beginPropertyRow(vultra::tr("common.value"));
                                    if (ImGui::ColorEdit4("##Value", &property.colorValue.x))
                                        changed = true;
                                    ui::endPropertyRow();
                                    break;
                                case vultra::MaterialPropertyBlockValueType::eTexture2D:
                                    if (ctx && textureSelector)
                                        changed |= ui::drawTextureUriField(
                                            *ctx, vultra::tr("inspector.material.texture"), property.textureUri, *textureSelector);
                                    else if (drawMaterialStringInput(vultra::tr("inspector.material.textureUri"), property.textureUri))
                                        changed = true;
                                    break;
                                case vultra::MaterialPropertyBlockValueType::eFloat:
                                default:
                                    ui::beginPropertyRow(vultra::tr("common.value"));
                                    if (ImGui::DragFloat("##Value", &property.floatValue, 0.01f))
                                        changed = true;
                                    ui::endPropertyRow();
                                    break;
                            }

                            if (ImGui::SmallButton((std::string {ICON_MDI_DELETE_OUTLINE " "} + vultra::tr("inspector.material.removeProperty")).c_str()))
                                removeProperty = propertyIndex;
                            ImGui::Separator();
                            ImGui::PopID();
                        }
                        if (removeProperty >= 0)
                        {
                            override.properties.erase(override.properties.begin() + removeProperty);
                            changed = true;
                        }
                        if (ImGui::SmallButton((std::string {ICON_MDI_PLUS " "} + vultra::tr("inspector.material.addProperty")).c_str()))
                        {
                            override.properties.push_back(vultra::MaterialPropertyBlockEntry {
                                .name       = "baseColor",
                                .type       = vultra::MaterialPropertyBlockValueType::eColor,
                                .colorValue = glm::vec4 {1.0f},
                            });
                            changed = true;
                        }
                        ImGui::TreePop();
                    }
                    if (ImGui::SmallButton((std::string {ICON_MDI_DELETE " "} + vultra::tr("common.remove")).c_str()))
                        removeIndex = i;
                    ImGui::Separator();
                    ImGui::PopID();
                }
                if (removeIndex >= 0)
                {
                    mesh.materialOverrides.erase(mesh.materialOverrides.begin() + removeIndex);
                    changed = true;
                }
                if (ImGui::Button((std::string {ICON_MDI_PLUS " "} + vultra::tr("inspector.material.addOverride")).c_str(),
                                  ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
                {
                    mesh.materialOverrides.push_back({});
                    changed = true;
                }
            }
            return changed;
        }

        bool drawUiLayoutComponentFields(vultra::UiLayoutComponent& layout)
        {
            bool changed = false;
            ui::beginPropertyRow(vultra::tr("common.enabled"));
            changed |= ImGui::Checkbox("##Enabled", &layout.enabled);
            ui::endPropertyRow();

            const std::string labels = std::string {vultra::tr("common.none")} + '\0' +
                                       vultra::tr("inspector.uiLayout.kind.horizontal") + '\0' +
                                       vultra::tr("inspector.uiLayout.kind.vertical") + '\0' +
                                       vultra::tr("inspector.uiLayout.kind.grid") + '\0';
            int index = static_cast<int>(std::min(layout.kind, 3u));
            ui::beginPropertyRow(vultra::tr("inspector.uiLayout.layout"));
            if (ImGui::Combo("##Layout", &index, labels.c_str()))
            {
                layout.kind = static_cast<uint32_t>(std::clamp(index, 0, 3));
                changed     = true;
            }
            ui::endPropertyRow();

            ui::beginPropertyRow(vultra::tr("inspector.uiLayout.paddingPx"));
            changed |= ImGui::DragFloat4("##PaddingPx", &layout.paddingPx.x, 0.5f);
            ui::endPropertyRow();
            ui::beginPropertyRow(vultra::tr("inspector.uiLayout.marginPx"));
            changed |= ImGui::DragFloat4("##MarginPx", &layout.marginPx.x, 0.5f);
            ui::endPropertyRow();
            ui::beginPropertyRow(vultra::tr("inspector.uiLayout.spacingPx"));
            changed |= ImGui::DragFloat("##SpacingPx", &layout.spacingPx, 0.5f);
            ui::endPropertyRow();
            ui::beginPropertyRow(vultra::tr("inspector.uiLayout.cellSizePx"));
            changed |= ImGui::DragFloat2("##CellSizePx", &layout.cellSizePx.x, 0.5f);
            ui::endPropertyRow();
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

        template<typename Component>
        AddComponentDescriptor addUiComponentDescriptor(const char* key, const char* label)
        {
            return AddComponentDescriptor {
                key,
                label,
                "UI",
                [](entt::registry& reg, entt::entity entity) { return reg.all_of<Component>(entity); },
                [](EditorContext&, entt::registry& reg, entt::entity entity) {
                    if (!reg.all_of<vultra::RectTransformComponent>(entity))
                        reg.emplace<vultra::RectTransformComponent>(entity);
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
                addComponentDescriptor<vultra::LayerComponent>("Layer", "Layer", "Core"),
                addComponentDescriptor<vultra::RectTransformComponent>("RectTransform", "Rect Transform", "UI"),
                addUiComponentDescriptor<vultra::CanvasComponent>("Canvas", "Canvas"),
                addUiComponentDescriptor<vultra::UiPanelComponent>("UiPanel", "UI Panel"),
                addUiComponentDescriptor<vultra::UiImageComponent>("UiImage", "UI Image"),
                addUiComponentDescriptor<vultra::UiTextComponent>("UiText", "UI Text"),
                addUiComponentDescriptor<vultra::UiButtonComponent>("UiButton", "UI Button"),
                addUiComponentDescriptor<vultra::UiToggleComponent>("UiToggle", "UI Toggle"),
                addUiComponentDescriptor<vultra::UiSliderComponent>("UiSlider", "UI Slider"),
                addUiComponentDescriptor<vultra::UiProgressBarComponent>("UiProgressBar", "UI Progress Bar"),
                addUiComponentDescriptor<vultra::UiLayoutComponent>("UiLayout", "UI Layout"),
                addComponentDescriptor<vultra::MeshComponent>("Mesh", "Mesh", "Rendering"),
                addAnimatorComponentDescriptor(),
                addComponentDescriptor<vultra::GaussianSplatComponent>(
                    "GaussianSplat", "Gaussian Splat", "Rendering"),
                addComponentDescriptor<vultra::EnvironmentComponent>("Environment", "Environment", "Lighting"),
                addComponentDescriptor<vultra::ReflectionProbeComponent>(
                    "ReflectionProbe", "Reflection Probe", "Lighting"),
                addComponentDescriptor<vultra::LightComponent>("Light", "Light", "Lighting"),
                addComponentDescriptor<vultra::ParticleEmitterComponent>(
                    "ParticleEmitter", "Particle Emitter", "Rendering"),
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
                addComponentDescriptor<vultra::AudioSourceComponent>("AudioSource", "Audio Source", "Audio"),
                addComponentDescriptor<vultra::AudioListenerComponent>("AudioListener", "Audio Listener", "Audio"),
            };
            return descriptors;
        }

        const std::vector<const char*>& componentDefaultOrder()
        {
            static const std::vector<const char*> order {
                "RectTransform",
                "Transform",
                "Layer",
                "Canvas",
                "UiPanel",
                "UiImage",
                "UiText",
                "UiButton",
                "UiToggle",
                "UiSlider",
                "UiProgressBar",
                "UiLayout",
                "Mesh",
                "Animator",
                "GaussianSplat",
                "Environment",
                "ReflectionProbe",
                "Light",
                "ParticleEmitter",
                "RigidBody",
                "BoxShape",
                "SphereShape",
                "CapsuleShape",
                "Camera",
                "XRView",
                "Script",
                "AudioSource",
                "AudioListener",
                "Prefab",
            };
            return order;
        }

        bool entityHasOrderedComponent(entt::registry& reg, entt::entity entity, const std::string& key)
        {
            if (key == "RectTransform")
                return reg.all_of<vultra::RectTransformComponent>(entity);
            if (key == "Transform")
                return reg.all_of<vultra::TransformComponent>(entity) &&
                       !reg.all_of<vultra::RectTransformComponent>(entity);
            if (key == "Layer")
                return reg.all_of<vultra::LayerComponent>(entity);
            if (key == "Canvas")
                return reg.all_of<vultra::CanvasComponent>(entity);
            if (key == "UiPanel")
                return reg.all_of<vultra::UiPanelComponent>(entity);
            if (key == "UiImage")
                return reg.all_of<vultra::UiImageComponent>(entity);
            if (key == "UiText")
                return reg.all_of<vultra::UiTextComponent>(entity);
            if (key == "UiButton")
                return reg.all_of<vultra::UiButtonComponent>(entity);
            if (key == "UiToggle")
                return reg.all_of<vultra::UiToggleComponent>(entity);
            if (key == "UiSlider")
                return reg.all_of<vultra::UiSliderComponent>(entity);
            if (key == "UiProgressBar")
                return reg.all_of<vultra::UiProgressBarComponent>(entity);
            if (key == "UiLayout")
                return reg.all_of<vultra::UiLayoutComponent>(entity);
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
            if (key == "ParticleEmitter")
                return reg.all_of<vultra::ParticleEmitterComponent>(entity);
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
            if (key == "AudioSource")
                return reg.all_of<vultra::AudioSourceComponent>(entity);
            if (key == "AudioListener")
                return reg.all_of<vultra::AudioListenerComponent>(entity);
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

        const char* componentKeyToTrKey(const std::string& key)
        {
            if (key == "RectTransform")
                return "inspector.component.rectTransform";
            if (key == "Transform")
                return "inspector.component.transform";
            if (key == "Layer")
                return "inspector.component.layer";
            if (key == "Canvas")
                return "inspector.component.canvas";
            if (key == "UiPanel")
                return "inspector.component.uiPanel";
            if (key == "UiImage")
                return "inspector.component.uiImage";
            if (key == "UiText")
                return "inspector.component.uiText";
            if (key == "UiButton")
                return "inspector.component.uiButton";
            if (key == "UiToggle")
                return "inspector.component.uiToggle";
            if (key == "UiSlider")
                return "inspector.component.uiSlider";
            if (key == "UiProgressBar")
                return "inspector.component.uiProgressBar";
            if (key == "UiLayout")
                return "inspector.component.uiLayout";
            if (key == "Mesh")
                return "inspector.component.mesh";
            if (key == "Animator")
                return "inspector.component.animator";
            if (key == "GaussianSplat")
                return "inspector.component.gaussianSplat";
            if (key == "Environment")
                return "inspector.component.environment";
            if (key == "ReflectionProbe")
                return "inspector.component.reflectionProbe";
            if (key == "Light")
                return "inspector.component.light";
            if (key == "ParticleEmitter")
                return "inspector.component.particleEmitter";
            if (key == "RigidBody")
                return "inspector.component.rigidBody";
            if (key == "BoxShape")
                return "inspector.component.boxShape";
            if (key == "SphereShape")
                return "inspector.component.sphereShape";
            if (key == "CapsuleShape")
                return "inspector.component.capsuleShape";
            if (key == "Camera")
                return "inspector.component.camera";
            if (key == "XRView")
                return "inspector.component.xrView";
            if (key == "Script")
                return "inspector.component.script";
            if (key == "AudioSource")
                return "inspector.component.audioSource";
            if (key == "AudioListener")
                return "inspector.component.audioListener";
            if (key == "Prefab")
                return "inspector.component.prefab";
            return nullptr;
        }

        const char* orderedComponentLabel(const std::string& key)
        {
            if (const char* trKey = componentKeyToTrKey(key))
                return vultra::tr(trKey);
            return vultra::tr("inspector.component.generic");
        }

        const char* addComponentCategoryLabel(const char* category)
        {
            if (category == nullptr)
                return "";
            if (std::strcmp(category, "Core") == 0)
                return vultra::trId("inspector.category.core", "Core");
            if (std::strcmp(category, "UI") == 0)
                return vultra::trId("inspector.category.ui", "UI");
            if (std::strcmp(category, "Rendering") == 0)
                return vultra::trId("inspector.category.rendering", "Rendering");
            if (std::strcmp(category, "Animation") == 0)
                return vultra::trId("inspector.category.animation", "Animation");
            if (std::strcmp(category, "Lighting") == 0)
                return vultra::trId("inspector.category.lighting", "Lighting");
            if (std::strcmp(category, "Physics") == 0)
                return vultra::trId("inspector.category.physics", "Physics");
            if (std::strcmp(category, "Camera") == 0)
                return vultra::trId("inspector.category.camera", "Camera");
            if (std::strcmp(category, "Scripting") == 0)
                return vultra::trId("inspector.category.scripting", "Scripting");
            if (std::strcmp(category, "Audio") == 0)
                return vultra::trId("inspector.category.audio", "Audio");
            return category;
        }

        void removeOrderedComponent(entt::registry& reg, entt::entity entity, const std::string& key)
        {
            if (key == "RectTransform")
                reg.remove<vultra::RectTransformComponent>(entity);
            else if (key == "Transform")
                reg.remove<vultra::TransformComponent>(entity);
            else if (key == "Layer")
                reg.remove<vultra::LayerComponent>(entity);
            else if (key == "Canvas")
                reg.remove<vultra::CanvasComponent>(entity);
            else if (key == "UiPanel")
                reg.remove<vultra::UiPanelComponent>(entity);
            else if (key == "UiImage")
                reg.remove<vultra::UiImageComponent>(entity);
            else if (key == "UiText")
                reg.remove<vultra::UiTextComponent>(entity);
            else if (key == "UiButton")
                reg.remove<vultra::UiButtonComponent>(entity);
            else if (key == "UiToggle")
                reg.remove<vultra::UiToggleComponent>(entity);
            else if (key == "UiSlider")
                reg.remove<vultra::UiSliderComponent>(entity);
            else if (key == "UiProgressBar")
                reg.remove<vultra::UiProgressBarComponent>(entity);
            else if (key == "UiLayout")
                reg.remove<vultra::UiLayoutComponent>(entity);
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
            else if (key == "ParticleEmitter")
                reg.remove<vultra::ParticleEmitterComponent>(entity);
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
            else if (key == "AudioSource")
                reg.remove<vultra::AudioSourceComponent>(entity);
            else if (key == "AudioListener")
                reg.remove<vultra::AudioListenerComponent>(entity);
        }

        std::string componentRemovalBlockReason(entt::registry& reg, entt::entity entity, const std::string& key)
        {
            if (key == "Prefab")
                return vultra::tr("inspector.removeBlock.prefab");

            if (key == "UiImage")
            {
                const auto* button = reg.try_get<vultra::UiButtonComponent>(entity);
                const auto* id     = reg.try_get<vultra::IDComponent>(entity);
                if (button && id && button->targetGraphic.valid() && button->targetGraphic == id->uuid)
                    return vultra::tr("inspector.removeBlock.uiButtonTargetGraphic");
            }

            return {};
        }

        bool orderedComponentHeader(const std::string& key,
                                    const std::size_t  index,
                                    const std::size_t  count,
                                    const char*        removeBlockReason,
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
            const bool removable = !removeBlockReason || removeBlockReason[0] == '\0';
            ImGui::BeginDisabled(!removable);
            if (ImGui::SmallButton(ICON_MDI_DELETE_OUTLINE))
                removeRequested = true;
            ImGui::EndDisabled();
            if (!removable && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", removeBlockReason);

            ImGui::PopID();
            return open;
        }
    } // namespace

    InspectorWindow::InspectorWindow() : EditorWindow("Inspector", ICON_MDI_TUNE, "window.inspector")
    {
        m_ModelPreviewWorld.setDebugName("Inspector Preview");
    }

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
        // Editing entity/component properties makes the scene the active undo/redo document.
        claimActiveDocument(ctx, ctx.sceneHistory, ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows));

        bool keepModelPreview = false;
        if (Selection::lastCategory() == SelectionCategory::Asset && ctx.services)
        {
            if (auto* assetService = ctx.services->tryGet<vultra::IAssetService>())
            {
                const auto entry = assetService->registry().lookup(Selection::lastId().native());
                keepModelPreview =
                    entry.type == vasset::VAssetType::eMesh || entry.type == vasset::VAssetType::eAnimation;
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
            ImGui::TextUnformatted(vultra::tr("inspector.project.title"));
            ImGui::Separator();
            ImGui::TextWrapped(
                "%s",
                vultra::trf("inspector.project.name",
                            state.currentProjectName.empty() ? vultra::tr("inspector.project.noProject") :
                                                               state.currentProjectName.c_str())
                    .c_str());
            ImGui::TextWrapped(
                "%s",
                vultra::trf("inspector.project.root",
                            state.currentProject.empty() ? vultra::tr("common.none") :
                                                           state.currentProject.generic_string().c_str())
                    .c_str());
            ImGui::TextWrapped("%s", vultra::trf("inspector.project.assetRoot", state.currentAssetRoot).c_str());
            ImGui::TextWrapped("%s", vultra::trf("inspector.project.defaultScene", state.currentDefaultScene).c_str());
        }

        ImGui::End();
    }

    void InspectorWindow::drawPrefabSection(EditorContext& ctx, vultra::World& world, entt::entity entity)
    {
        auto& reg = world.registry();

        // Walk ancestors to find the prefab instance root (if any).
        entt::entity rootEnt = entt::null;
        std::string  prefabUri;
        for (entt::entity cur = entity; cur != entt::null && reg.valid(cur); cur = world.parent(cur))
        {
            if (auto* pic = reg.try_get<vultra::PrefabInstanceComponent>(cur))
            {
                rootEnt   = cur;
                prefabUri = pic->prefabUri;
                break;
            }
        }
        if (rootEnt == entt::null)
            return;

        const ImVec4 prefabBlue {0.40f, 0.62f, 1.00f, 1.0f};
        ImGui::PushStyleColor(ImGuiCol_Text, prefabBlue);
        ImGui::TextUnformatted(ICON_MDI_CUBE "  Prefab Instance");
        ImGui::PopStyleColor();
        if (!prefabUri.empty())
            ImGui::TextDisabled("%s", prefabUri.c_str());

        if (entity == rootEnt && ImGui::SmallButton(ICON_MDI_CUBE_OFF_OUTLINE "  Unpack"))
            (void)ctx.editor->executeCommand(
                ctx, "scene.unpack_prefab", {{"entity", static_cast<uint32_t>(entity)}});

        std::unordered_set<std::string> overrides;
        if (auto* sceneService = ctx.services->tryGet<vultra::ISceneService>())
            overrides = sceneService->prefabOverriddenFields(world, entity);

        if (!overrides.empty() &&
            ImGui::CollapsingHeader("Prefab Overrides", ImGuiTreeNodeFlags_DefaultOpen))
        {
            std::vector<std::string> keys(overrides.begin(), overrides.end());
            std::sort(keys.begin(), keys.end());
            for (const auto& key : keys)
            {
                const auto slash = key.find('/');
                if (slash == std::string::npos)
                    continue;
                const std::string component = key.substr(0, slash);
                const std::string field     = key.substr(slash + 1);

                ImGui::PushID(key.c_str());
                ImGui::TextColored(prefabBlue, "%s", key.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_MDI_UNDO))
                    (void)ctx.editor->executeCommand(ctx,
                                                     "scene.revert_override",
                                                     {{"entity", static_cast<uint32_t>(entity)},
                                                      {"component", component},
                                                      {"field", field}});
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Revert to prefab value");
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_MDI_CHECK))
                    (void)ctx.editor->executeCommand(ctx,
                                                     "scene.apply_override",
                                                     {{"entity", static_cast<uint32_t>(entity)},
                                                      {"component", component},
                                                      {"field", field}});
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Apply to prefab");
                ImGui::PopID();
            }
        }
        ImGui::Separator();
    }

    void InspectorWindow::drawEntityInspector(EditorContext& ctx)
    {
        if (!ctx.services)
        {
            ImGui::TextUnformatted(vultra::tr("inspector.servicesUnavailable"));
            return;
        }

        auto* worldService = ctx.services->tryGet<vultra::IWorldService>();
        if (!worldService)
        {
            ImGui::TextUnformatted(vultra::tr("inspector.worldServiceUnavailable"));
            return;
        }

        auto& world = worldService->world();
        auto& reg   = world.registry();
        auto  e     = findEntityByUUID(world, Selection::lastId());
        if (e == entt::null || !reg.valid(e))
        {
            ImGui::TextUnformatted(vultra::tr("inspector.entityGone"));
            return;
        }

        ui::sectionTitle(ICON_MDI_CUBE_OUTLINE, vultra::tr("inspector.entity.title"));

        if (auto* id = reg.try_get<vultra::IDComponent>(e))
            ImGui::TextWrapped("%s", vultra::trf("inspector.entity.uuid", id->uuid.toString()).c_str());

        auto& name = reg.get_or_emplace<vultra::NameComponent>(e, vultra::NameComponent {"Entity"});
        if (m_NameEditEntity != Selection::lastId())
        {
            m_NameEditEntity = Selection::lastId();
            copyName(m_NameBuffer, name.name);
        }
        ui::beginPropertyRow(vultra::tr("common.name"));
        const bool nameChanged = ImGui::InputText("##Name", m_NameBuffer.data(), m_NameBuffer.size());
        ui::endPropertyRow();
        if (nameChanged)
        {
            name.name            = m_NameBuffer.data();
            ctx.state.sceneDirty = true;
            if (ctx.history)
                ctx.history->setNextLabel("Rename Entity");
        }

        drawPrefabSection(ctx, world, e);

        auto& status = reg.get_or_emplace<vultra::EntityStatusComponent>(e);
        if (ImGui::CollapsingHeader(vultra::tr("inspector.component.status"), ImGuiTreeNodeFlags_DefaultOpen))
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
            ImGui::TextUnformatted(vultra::trf("inspector.hierarchy.children", h ? h->childCount : 0).c_str());
            if (h && h->parent != entt::null)
            {
                if (auto* parentId = reg.try_get<vultra::IDComponent>(h->parent))
                    ImGui::TextWrapped("%s", vultra::trf("inspector.hierarchy.parentUuid", parentId->uuid.toString()).c_str());
            }
            else
            {
                ImGui::TextUnformatted(vultra::tr("inspector.hierarchy.parentSceneRoot"));
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
            const auto        removeBlockReason = componentRemovalBlockReason(reg, e, key);
            const bool        open              = orderedComponentHeader(
                key,
                i,
                componentOrder.size(),
                removeBlockReason.c_str(),
                removeRequested,
                moveUpRequested,
                moveDownRequested);

            if (moveUpRequested && i > 0)
            {
                std::swap(componentOrder[i], componentOrder[i - 1]);
                ctx.state.statusMessage = vultra::trf("inspector.componentMovedUp", orderedComponentLabel(key));
                break;
            }
            if (moveDownRequested && i + 1 < componentOrder.size())
            {
                std::swap(componentOrder[i], componentOrder[i + 1]);
                ctx.state.statusMessage = vultra::trf("inspector.componentMovedDown", orderedComponentLabel(key));
                break;
            }

            if (removeRequested)
            {
                removeOrderedComponent(reg, e, key);
                componentOrder.erase(componentOrder.begin() + static_cast<std::ptrdiff_t>(i));
                ctx.state.sceneDirty    = true;
                ctx.state.statusMessage = vultra::trf("inspector.componentRemoved", orderedComponentLabel(key));
                if (ctx.history)
                    ctx.history->setNextLabel(ctx.state.statusMessage);
                break;
            }

            if (!open)
                continue;

            // Indent all component field bodies uniformly so labels/controls line up
            // across every component (see ai/knowledge/editor-ui-style.md). Individual
            // field drawers must NOT add their own Indent.
            ImGui::Indent();

            if (key == "RectTransform")
            {
                if (auto* rect = reg.try_get<vultra::RectTransformComponent>(e))
                    if (drawRectTransformComponentFields(*rect))
                    {
                        ctx.state.sceneDirty = true;
                        if (ctx.history)
                            ctx.history->setNextLabel("Edit Rect Transform");
                    }
            }
            else if (key == "Transform")
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
            else if (key == "Layer")
            {
                if (auto* layer = reg.try_get<vultra::LayerComponent>(e))
                    if (drawMetaFields(&ctx, &m_TextureSelector, *layer))
                    {
                        ctx.state.sceneDirty = true;
                        if (ctx.history)
                            ctx.history->setNextLabel("Edit Layer");
                    }
            }
            else if (key == "Canvas")
            {
                if (auto* canvas = reg.try_get<vultra::CanvasComponent>(e))
                    if (drawMetaFields(&ctx, &m_TextureSelector, *canvas))
                    {
                        ctx.state.sceneDirty = true;
                        if (ctx.history)
                            ctx.history->setNextLabel("Edit Canvas");
                    }
            }
            else if (key == "UiPanel")
            {
                if (auto* panel = reg.try_get<vultra::UiPanelComponent>(e))
                    if (drawMetaFields(&ctx, &m_TextureSelector, *panel))
                    {
                        ctx.state.sceneDirty = true;
                        if (ctx.history)
                            ctx.history->setNextLabel("Edit UI Panel");
                    }
            }
            else if (key == "UiImage")
            {
                if (auto* image = reg.try_get<vultra::UiImageComponent>(e))
                    if (drawMetaFields(&ctx, &m_TextureSelector, *image))
                    {
                        ctx.state.sceneDirty = true;
                        if (ctx.history)
                            ctx.history->setNextLabel("Edit UI Image");
                    }
            }
            else if (key == "UiText")
            {
                if (auto* text = reg.try_get<vultra::UiTextComponent>(e))
                    if (drawMetaFields(&ctx, &m_TextureSelector, *text))
                    {
                        ctx.state.sceneDirty = true;
                        if (ctx.history)
                            ctx.history->setNextLabel("Edit UI Text");
                    }
            }
            else if (key == "UiButton")
            {
                if (auto* button = reg.try_get<vultra::UiButtonComponent>(e))
                    if (drawMetaFields(&ctx, &m_TextureSelector, *button))
                    {
                        ctx.state.sceneDirty = true;
                        if (ctx.history)
                            ctx.history->setNextLabel("Edit UI Button");
                    }
            }
            else if (key == "UiToggle")
            {
                if (auto* toggle = reg.try_get<vultra::UiToggleComponent>(e))
                    if (drawMetaFields(&ctx, &m_TextureSelector, *toggle))
                    {
                        ctx.state.sceneDirty = true;
                        if (ctx.history)
                            ctx.history->setNextLabel("Edit UI Toggle");
                    }
            }
            else if (key == "UiSlider")
            {
                if (auto* slider = reg.try_get<vultra::UiSliderComponent>(e))
                    if (drawMetaFields(&ctx, &m_TextureSelector, *slider))
                    {
                        ctx.state.sceneDirty = true;
                        if (ctx.history)
                            ctx.history->setNextLabel("Edit UI Slider");
                    }
            }
            else if (key == "UiProgressBar")
            {
                if (auto* progress = reg.try_get<vultra::UiProgressBarComponent>(e))
                    if (drawMetaFields(&ctx, &m_TextureSelector, *progress))
                    {
                        ctx.state.sceneDirty = true;
                        if (ctx.history)
                            ctx.history->setNextLabel("Edit UI Progress Bar");
                    }
            }
            else if (key == "UiLayout")
            {
                if (auto* layout = reg.try_get<vultra::UiLayoutComponent>(e))
                    if (drawUiLayoutComponentFields(*layout))
                    {
                        ctx.state.sceneDirty = true;
                        if (ctx.history)
                            ctx.history->setNextLabel("Edit UI Layout");
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
                    if (drawAnimatorComponentFields(ctx, reg, e, *animator))
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
            else if (key == "ParticleEmitter")
            {
                if (auto* emitter = reg.try_get<vultra::ParticleEmitterComponent>(e))
                    if (drawMetaFields(&ctx, &m_TextureSelector, *emitter))
                    {
                        ctx.state.sceneDirty = true;
                        if (ctx.history)
                            ctx.history->setNextLabel("Edit Particle Emitter");
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
                    if (ImGui::Button((std::string {ICON_MDI_CAMERA_SWITCH "  "} + vultra::tr("inspector.camera.alignWithSceneView")).c_str(),
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
                        ImGui::SetTooltip("%s", vultra::tr("inspector.camera.alignWithSceneViewTip"));
                    if (ImGui::Button((std::string {ICON_MDI_CROSSHAIRS_GPS "  "} + vultra::tr("inspector.camera.alignSceneViewWithCamera")).c_str(),
                                      ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
                    {
                        alignSceneViewToCameraEntity(ctx, world, e);
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                        ImGui::SetTooltip("%s", vultra::tr("inspector.camera.alignSceneViewWithCameraTip"));
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
            else if (key == "AudioSource")
            {
                if (auto* audioSource = reg.try_get<vultra::AudioSourceComponent>(e))
                    if (drawMetaFields(&ctx, &m_TextureSelector, *audioSource))
                    {
                        ctx.state.sceneDirty = true;
                        if (ctx.history)
                            ctx.history->setNextLabel("Edit Audio Source");
                    }
            }
            else if (key == "AudioListener")
            {
                if (auto* audioListener = reg.try_get<vultra::AudioListenerComponent>(e))
                    if (drawMetaFields(&ctx, &m_TextureSelector, *audioListener))
                    {
                        ctx.state.sceneDirty = true;
                        if (ctx.history)
                            ctx.history->setNextLabel("Edit Audio Listener");
                    }
            }
            else if (key == "Prefab")
            {
                if (auto* prefab = reg.try_get<vultra::PrefabInstanceComponent>(e))
                {
                    ImGui::TextWrapped("%s", vultra::trf("inspector.prefab.uri", prefab->prefabUri).c_str());
                    if (prefab->prefabId.valid())
                        ImGui::TextWrapped("%s", vultra::trf("inspector.prefab.uuid", prefab->prefabId.toString()).c_str());
                }
            }

            ImGui::Unindent();
        }

        ImGui::Spacing();
        drawAddComponentButton(ctx, world, e);
    }

    void InspectorWindow::drawAddComponentButton(EditorContext& ctx, vultra::World& world, const entt::entity entity)
    {
        auto& reg = world.registry();

        const float width = ImGui::GetContentRegionAvail().x;
        if (ImGui::Button((std::string {ICON_MDI_PLUS "  "} + vultra::tr("inspector.addComponent")).c_str(), ImVec2(width, 0.0f)))
            ImGui::OpenPopup("AddComponentPopup");

        if (ImGui::BeginPopup("AddComponentPopup"))
        {
            bool any = false;
            constexpr const char* categories[] = {
                "Core",
                "UI",
                "Rendering",
                "Animation",
                "Lighting",
                "Physics",
                "Camera",
                "Scripting",
                "Audio",
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
                if (ImGui::BeginMenu(addComponentCategoryLabel(category)))
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
                        const char* descTrKey   = desc.key ? componentKeyToTrKey(desc.key) : nullptr;
                        const char* descLabel   = descTrKey ? vultra::tr(descTrKey) : desc.label;
                        const char* descMenuLbl = descTrKey ? vultra::trId(descTrKey, desc.key) : desc.label;
                        if (ImGui::MenuItem(descMenuLbl))
                        {
                            desc.add(ctx, reg, entity);
                            auto& order = m_ComponentOrder[Selection::lastId()];
                            if (desc.key && std::find(order.begin(), order.end(), desc.key) == order.end())
                                order.emplace_back(desc.key);
                            ctx.state.sceneDirty    = true;
                            ctx.state.statusMessage = vultra::trf("inspector.componentAdded", descLabel);
                            if (desc.key &&
                                (std::strcmp(desc.key, "BoxShape") == 0 ||
                                 std::strcmp(desc.key, "SphereShape") == 0 ||
                                 std::strcmp(desc.key, "CapsuleShape") == 0) &&
                                reg.all_of<vultra::MeshComponent>(entity))
                            {
                                ctx.state.statusMessage += vultra::tr("inspector.fitToMeshBoundsSuffix");
                            }
                            if (ctx.history)
                                ctx.history->setNextLabel(ctx.state.statusMessage);
                            ImGui::CloseCurrentPopup();
                        }
                        if (xrViewRequiresCamera)
                        {
                            ImGui::EndDisabled();
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                                ImGui::SetTooltip("%s", vultra::tr("inspector.xrViewRequiresCamera"));
                        }
                    }
                    ImGui::EndMenu();
                }
            }

            if (!any)
                ImGui::TextDisabled("%s", vultra::tr("inspector.allComponentsPresent"));

            ImGui::EndPopup();
        }
    }

    void InspectorWindow::drawAssetInspector(EditorContext& ctx)
    {
        if (!ctx.services)
        {
            ImGui::TextUnformatted(vultra::tr("inspector.servicesUnavailable"));
            return;
        }

        auto* assetService = ctx.services->tryGet<vultra::IAssetService>();
        if (!assetService)
        {
            ImGui::TextUnformatted(vultra::tr("inspector.assetServiceUnavailable"));
            return;
        }

        const auto uuid  = Selection::lastId();
        const auto entry = assetService->registry().lookup(uuid.native());

        ui::sectionTitle(ICON_MDI_PACKAGE_VARIANT_CLOSED, vultra::tr("inspector.asset.title"));
        ImGui::TextWrapped("%s", vultra::trf("inspector.asset.uuid", uuid.toString()).c_str());
        ImGui::TextWrapped("%s", vultra::trf("inspector.asset.type", vasset::toString(entry.type)).c_str());
        ImGui::TextWrapped("%s", vultra::trf("inspector.asset.source", entry.sourcePath).c_str());
        ImGui::TextWrapped("%s", vultra::trf("inspector.asset.imported", entry.importedPath).c_str());

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

        ImGui::TextUnformatted(vultra::tr("inspector.preview"));
        const float size = std::min(ImGui::GetContentRegionAvail().x, vultra::ui::dp(260.0f));
        ImGui::Image(previewId, ImVec2(size, size));
        (void)ui::capturePreviewItemInput();
    }

    void InspectorWindow::drawSkeletonAssetInspector(EditorContext& ctx, const vasset::VAssetRegistry::AssetEntry& entry)
    {
        const auto assetRoot = ctx.state.currentProject / ctx.state.currentAssetRoot;
        const auto path = assetRoot / std::filesystem::path(entry.importedPath);
        vasset::VSkeleton skeleton;
        const auto result = vasset::loadSkeleton(path.generic_string(), skeleton);
        if (!result)
        {
            ImGui::TextDisabled("%s", vultra::tr("inspector.skeleton.unavailable"));
            return;
        }

        ui::sectionTitle(ICON_MDI_SOURCE_BRANCH, vultra::tr("inspector.skeleton.title"));
        ImGui::TextWrapped("%s", vultra::trf("inspector.skeleton.name", skeleton.name).c_str());
        ImGui::TextUnformatted(vultra::trf("inspector.skeleton.joints", skeleton.jointNames.size()).c_str());
        ImGui::TextUnformatted(vultra::trf("inspector.skeleton.payload", formatFileSize(skeleton.ozzData.size())).c_str());
    }

    void InspectorWindow::drawAnimationAssetInspector(EditorContext& ctx, const vasset::VAssetRegistry::AssetEntry& entry)
    {
        const auto assetRoot = ctx.state.currentProject / ctx.state.currentAssetRoot;
        const auto path = assetRoot / std::filesystem::path(entry.importedPath);
        vasset::VAnimation animation;
        const auto result = vasset::loadAnimation(path.generic_string(), animation);
        if (!result)
        {
            ImGui::TextDisabled("%s", vultra::tr("inspector.animation.unavailable"));
            return;
        }

        ui::sectionTitle(ICON_MDI_PLAY, vultra::tr("inspector.animation.title"));
        ImGui::TextWrapped("%s", vultra::trf("inspector.animation.name", animation.name).c_str());
        ImGui::TextUnformatted(vultra::trf("inspector.animation.duration", animation.duration).c_str());
        ImGui::TextUnformatted(vultra::trf("inspector.animation.payload", formatFileSize(animation.ozzData.size())).c_str());

        const auto uuid = Selection::lastId();
        const auto key  = "animation:" + uuid.toString() + ":" + entry.importedPath;
        if (m_ModelPreviewKey != key)
            rebuildModelPreviewWorldForAnimation(ctx, uuid, entry);

        ImGui::Spacing();
        ui::sectionTitle(ICON_MDI_PLAY, vultra::tr("inspector.preview"));
        if (ImGui::SmallButton(m_ModelPreviewAnimationPlaying ? ICON_MDI_PAUSE : ICON_MDI_PLAY))
        {
            m_ModelPreviewAnimationPlaying = !m_ModelPreviewAnimationPlaying;
            m_ModelPreviewDirty            = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_MDI_RESTORE "##ResetAnimationPreview"))
        {
            resetPreviewAnimators(m_ModelPreviewWorld);
            m_ModelPreviewDirty = true;
        }
        bool controlsChanged = false;
        ui::beginPropertyRow(vultra::tr("inspector.animator.loop"));
        controlsChanged |= ImGui::Checkbox("##Loop", &m_ModelPreviewAnimationLoop);
        ui::endPropertyRow();
        ui::beginPropertyRow(vultra::tr("inspector.animator.speed"));
        controlsChanged |= ImGui::DragFloat("##Speed", &m_ModelPreviewAnimationSpeed, 0.02f, 0.05f, 4.0f, "%.2f");
        ui::endPropertyRow();
        m_ModelPreviewAnimationSpeed = std::clamp(m_ModelPreviewAnimationSpeed, 0.05f, 4.0f);
        if (controlsChanged)
            m_ModelPreviewDirty = true;
        applyPreviewAnimatorControls(m_ModelPreviewWorld,
                                     m_ModelPreviewAnimationPlaying,
                                     m_ModelPreviewAnimationLoop,
                                     m_ModelPreviewAnimationSpeed);
        drawModelPreviewViewport(ctx, key);
    }

    void InspectorWindow::drawSourceAssetInspector(EditorContext& ctx)
    {
        const auto& path = ctx.state.selectedSourceAsset;
        const auto  ext  = path.extension().generic_string();

        std::error_code ec;
        const bool      isDir = std::filesystem::is_directory(path, ec);
        ui::sectionTitle(ui::sourceAssetIcon(path, isDir), vultra::tr("inspector.sourceAsset.title"));
        ImGui::TextWrapped("%s", vultra::trf("inspector.sourceAsset.name", path.filename().generic_string()).c_str());
        ImGui::TextWrapped(
            "%s",
            vultra::trf("inspector.sourceAsset.type", ext.empty() ? std::string {vultra::tr("inspector.sourceAsset.folder")} : ext)
                .c_str());
        ImGui::TextWrapped("%s", vultra::trf("inspector.sourceAsset.path", path.generic_string()).c_str());

        if (std::filesystem::is_regular_file(path, ec))
            ImGui::TextUnformatted(
                vultra::trf("inspector.sourceAsset.size", formatFileSize(std::filesystem::file_size(path, ec))).c_str());

        const bool renderGraphPassSource = std::filesystem::is_regular_file(path, ec) && fileLooksLikeRenderGraphPass(path);
        const bool materialAssetSource   = std::filesystem::is_regular_file(path, ec) && isMaterialAssetSource(path);
        const bool materialGraphNodeSource =
            std::filesystem::is_regular_file(path, ec) && isMaterialGraphNodeAssetSource(path);
        if (materialAssetSource)
        {
            ImGui::Spacing();
            drawMaterialAssetSourceInspector(ctx, path);
        }

        if (materialGraphNodeSource)
        {
            ImGui::Spacing();
            drawMaterialGraphNodeSourceInspector(ctx, path);
        }

        if (renderGraphPassSource)
        {
            ImGui::Spacing();
            drawRenderGraphPassSourceInspector(ctx, path);
        }

        if (std::filesystem::is_regular_file(path, ec) && isEditableSourceText(path))
        {
            const bool        sceneSource = sourceAssetHasExtension(path, {".vscn"});
            const std::string buttonText =
                sceneSource ? std::string {ICON_MDI_FILE_DOCUMENT_EDIT " "} + vultra::tr("inspector.sourceAsset.editAsSource") :
                renderGraphPassSource ? std::string {ICON_MDI_CODE_BRACES " "} + vultra::tr("inspector.sourceAsset.openLuaSource") :
                                        std::string {ICON_MDI_FILE_DOCUMENT_EDIT " "} + vultra::tr("inspector.sourceAsset.openInCodeEditor");
            if (ImGui::Button(buttonText.c_str()))
            {
                ctx.state.codeEditorPath          = path.lexically_normal();
                ctx.state.codeEditorOpenRequested = true;
                ctx.state.statusMessage =
                    sceneSource ? vultra::trf("inspector.sourceAsset.editingScene", path.filename().generic_string()) :
                                  vultra::trf("inspector.sourceAsset.openedInEditor", path.filename().generic_string());
            }
        }

        if (ui::isTextureSourceAsset(path))
        {
            ImGui::Spacing();
            drawSourceTextureImportInspector(ctx, path);
            ImGui::Spacing();
            drawSourceTexturePreview(ctx, path);
        }
        else if (!isDir && isModelSourceAsset(path))
        {
            ImGui::Spacing();
            drawSourceMeshImportInspector(ctx, path);
            ImGui::Spacing();
            drawSourceModelPreview(ctx, path);
        }
        else if (!isDir && isAudioSourceAsset(path))
        {
            ImGui::Spacing();
            drawSourceAudioImportInspector(ctx, path);
        }
        else if (sourceAssetHasExtension(path, {".vscn"}))
        {
            ImGui::Spacing();
            ImGui::TextUnformatted(vultra::tr("inspector.sceneSource.title"));
            if (ImGui::Button(vultra::tr("inspector.sceneSource.setAsDefault")))
            {
                const auto      assetRoot = ctx.state.currentProject / ctx.state.currentAssetRoot;
                std::error_code relEc;
                auto            rel = std::filesystem::relative(path, assetRoot, relEc);
                if (!relEc)
                {
                    ctx.state.currentDefaultScene = "res://" + rel.generic_string();
                    ctx.state.statusMessage = vultra::trf("inspector.sceneSource.defaultSet", ctx.state.currentDefaultScene);
                }
            }
        }
    }

    bool InspectorWindow::drawMaterialAssetSourceInspector(EditorContext& ctx, const std::filesystem::path& path)
    {
        struct MaterialAssetEditState
        {
            std::filesystem::path path;
            nlohmann::json        doc;
            std::string           error;
            std::vector<std::string> diagnostics;
            bool                  valid {false};
            bool                  dirty {false};
        };

        static MaterialAssetEditState state;
        const auto normalized = path.lexically_normal();
        if (state.path != normalized)
        {
            state       = {};
            state.path  = normalized;
            state.valid = readJsonFile(normalized, state.doc, state.error);
            if (state.valid)
            {
                if (!state.doc.contains("type"))
                    state.doc["type"] = "Material";
                if (!state.doc.contains("version"))
                    state.doc["version"] = 1;
                if (!state.doc.contains("properties") || !state.doc["properties"].is_object())
                    state.doc["properties"] = nlohmann::json::object();
                state.diagnostics = vultra::material::materialAssetFromJson(state.doc).diagnostics;
            }
        }

        ui::sectionTitle(ICON_MDI_PALETTE_SWATCH, vultra::tr("inspector.component.material"));
        if (!state.valid)
        {
            ImGui::TextWrapped("%s", vultra::trf("inspector.material.parseFailed", state.error).c_str());
            if (ImGui::Button((std::string {ICON_MDI_REFRESH " "} + vultra::tr("inspector.reload")).c_str()))
            {
                state.valid = readJsonFile(normalized, state.doc, state.error);
                state.dirty = false;
            }
            return false;
        }

        bool dirty = false;

        auto materialName = state.doc.value("name", normalized.stem().generic_string());
        if (drawMaterialStringInput(vultra::tr("common.name"), materialName))
        {
            state.doc["name"] = materialName;
            dirty             = true;
        }

        auto source = materialSourceFromJson(state.doc);
        state.diagnostics = vultra::material::materialAssetFromJson(state.doc).diagnostics;
        if (!state.diagnostics.empty())
        {
            ImGui::Spacing();
            ImGui::TextUnformatted(vultra::tr("inspector.diagnostics"));
            for (const auto& diagnostic : state.diagnostics)
                ImGui::BulletText("%s", diagnostic.c_str());
        }

        int  kind   = source.kind == vultra::material::MaterialSourceKind::eShader ? 1 :
                      source.kind == vultra::material::MaterialSourceKind::eGraph  ? 2 :
                                                                                     0;
        const std::string kinds = std::string {vultra::tr("inspector.material.kind.builtin")} + '\0' +
                                  vultra::tr("inspector.material.kind.shader") + '\0' +
                                  vultra::tr("inspector.material.kind.graph") + '\0';
        ui::beginPropertyRow(vultra::tr("inspector.material.source"));
        const bool sourceChanged = ImGui::Combo("##Source", &kind, kinds.c_str());
        ui::endPropertyRow();
        if (sourceChanged)
        {
            source.kind = kind == 1 ? vultra::material::MaterialSourceKind::eShader :
                          kind == 2 ? vultra::material::MaterialSourceKind::eGraph :
                                      vultra::material::MaterialSourceKind::eBuiltin;
            if (source.kind == vultra::material::MaterialSourceKind::eBuiltin && source.id.empty())
                source.id = "builtin/pbr";
            materialSourceToJson(source, state.doc);
            dirty = true;
        }

        if (source.kind == vultra::material::MaterialSourceKind::eBuiltin)
        {
            if (source.id.empty())
                source.id = "builtin/pbr";
            const char* builtinIds[] = {"builtin/pbr"};
            int         builtinIndex = source.id == "builtin/pbr" ? 0 : -1;
            ui::beginPropertyRow(vultra::tr("inspector.material.kind.builtin"));
            const bool builtinChanged = ImGui::Combo("##Builtin", &builtinIndex, builtinIds, IM_ARRAYSIZE(builtinIds));
            ui::endPropertyRow();
            if (builtinChanged && builtinIndex == 0)
            {
                source.id = "builtin/pbr";
                materialSourceToJson(source, state.doc);
                dirty = true;
            }
            if (source.id != "builtin/pbr")
            {
                if (drawMaterialStringInput(vultra::tr("inspector.material.customBuiltinId"), source.id))
                {
                    materialSourceToJson(source, state.doc);
                    dirty = true;
                }
            }
        }
        else if (source.kind == vultra::material::MaterialSourceKind::eGraph)
        {
            if (drawMaterialGraphUriField(&ctx, source.uri, vultra::tr("inspector.material.graph")))
            {
                materialSourceToJson(source, state.doc);
                dirty = true;
            }
        }
        else
        {
            std::array<char, 128> library {};
            std::array<char, 128> shader {};
            copyName(library, source.shaderLibrary.empty() ? std::string {"project"} : source.shaderLibrary);
            copyName(shader, source.id);
            if (drawShaderLibrarySelector(vultra::tr("inspector.shader.library"), library))
            {
                source.shaderLibrary = library.data();
                materialSourceToJson(source, state.doc);
                dirty = true;
            }
            if (drawLibraryShaderSelector(ctx, vultra::tr("inspector.shader.shader"), "frag", library, shader, true))
            {
                source.shaderLibrary = library.data();
                source.id            = shader.data();
                materialSourceToJson(source, state.doc);
                dirty = true;
            }
        }

        auto schema = resolveMaterialSchemaForEditor(ctx, source);
        if (schema.parameters.empty())
        {
            ImGui::Spacing();
            if (source.kind == vultra::material::MaterialSourceKind::eShader)
            {
                ImGui::TextWrapped("%s", vultra::tr("inspector.material.noShaderParams"));
            }
            else
            {
                ImGui::TextWrapped("%s", vultra::tr("inspector.material.noExposedParams"));
            }
        }
        else
        {
            ImGui::Spacing();
            ImGui::TextUnformatted(vultra::tr("inspector.material.properties"));
            auto& properties = state.doc["properties"];
            if (ImGui::BeginTable("MaterialProperties", 2, ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn(vultra::tr("common.name"), ImGuiTableColumnFlags_WidthFixed, vultra::ui::dp(170.0f));
                ImGui::TableSetupColumn(vultra::tr("common.value"), ImGuiTableColumnFlags_WidthStretch);
                for (const auto& param : schema.parameters)
                {
                    const auto label = param.displayName.empty() ? param.name : param.displayName;
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(label.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::PushID(param.name.c_str());
                    ImGui::SetNextItemWidth(-1.0f);
                    switch (param.type)
                    {
                        case vultra::material::MaterialPropertyType::eInt: {
                            int value = intFromJsonProperty(properties, param);
                            if (ImGui::InputInt("##value", &value))
                            {
                                properties[param.name] = value;
                                dirty                  = true;
                            }
                            break;
                        }
                        case vultra::material::MaterialPropertyType::eFloat: {
                            float value = floatFromJsonProperty(properties, param);
                            const bool changed = param.hasUiRange ?
                                                     ImGui::SliderFloat("##value", &value, param.uiMin, param.uiMax) :
                                                     ImGui::DragFloat("##value", &value, 0.01f);
                            if (changed)
                            {
                                properties[param.name] = value;
                                dirty                  = true;
                            }
                            break;
                        }
                        case vultra::material::MaterialPropertyType::eBool: {
                            bool value = boolFromJsonProperty(properties, param);
                            if (ImGui::Checkbox("##value", &value))
                            {
                                properties[param.name] = value;
                                dirty                  = true;
                            }
                            break;
                        }
                        case vultra::material::MaterialPropertyType::eVec2: {
                            auto value = vec2FromJsonProperty(properties, param);
                            if (ImGui::DragFloat2("##value", &value.x, 0.01f))
                            {
                                properties[param.name] = jsonFromVec2(value);
                                dirty                  = true;
                            }
                            break;
                        }
                        case vultra::material::MaterialPropertyType::eVec3: {
                            auto value = vec3FromJsonProperty(properties, param);
                            if (ImGui::DragFloat3("##value", &value.x, 0.01f))
                            {
                                properties[param.name] = jsonFromVec3(value);
                                dirty                  = true;
                            }
                            break;
                        }
                        case vultra::material::MaterialPropertyType::eColor:
                        case vultra::material::MaterialPropertyType::eVec4: {
                            auto value = vec4FromJsonProperty(properties, param);
                            const bool changed = param.type == vultra::material::MaterialPropertyType::eColor ?
                                                     ImGui::ColorEdit4("##value", &value.x) :
                                                     ImGui::DragFloat4("##value", &value.x, 0.01f);
                            if (changed)
                            {
                                properties[param.name] = jsonFromVec4(value);
                                dirty                  = true;
                            }
                            break;
                        }
                        case vultra::material::MaterialPropertyType::eTexture2D: {
                            auto        value = stringFromJsonProperty(properties, param.name);
                            const float clearButtonSize = ImGui::GetFrameHeight();
                            const float width = std::max(
                                1.0f,
                                ImGui::GetContentRegionAvail().x - clearButtonSize - ImGui::GetStyle().ItemSpacing.x);
                            bool changed = ui::drawTextureUriSelector(
                                ctx, "TextureSelectorPopup", value, m_TextureSelector, ImVec2(width, vultra::ui::dp(40.0f)));
                            ImGui::SameLine();
                            if (ImGui::SmallButton(ICON_MDI_CLOSE) && !value.empty())
                            {
                                value.clear();
                                changed = true;
                            }
                            if (changed)
                            {
                                if (value.empty())
                                    properties.erase(param.name);
                                else
                                    properties[param.name] = value;
                                dirty = true;
                            }
                            break;
                        }
                        default:
                            ImGui::TextDisabled("%s", vultra::tr("inspector.material.unsupportedType"));
                            break;
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }

        state.dirty = state.dirty || dirty;
        ImGui::Spacing();
        ImGui::BeginDisabled(!state.dirty);
        if (ImGui::Button((std::string {ICON_MDI_CONTENT_SAVE " "} + vultra::tr("inspector.material.save")).c_str()))
        {
            std::string error;
            if (writeJsonFile(state.path, state.doc, error))
            {
                state.dirty = false;
                state.error.clear();
                if (auto* assets = ctx.services ? ctx.services->tryGet<vultra::IAssetService>() : nullptr)
                {
                    const auto assetRoot = ctx.state.currentProject / ctx.state.currentAssetRoot;
                    std::error_code relEc;
                    const auto rel = std::filesystem::relative(state.path, assetRoot, relEc);
                    if (!relEc)
                        assets->clearTextAssetOverride("res://" + rel.generic_string());
                }
                ctx.state.statusMessage = vultra::trf("inspector.material.saved", state.path.filename().generic_string());
            }
            else
            {
                state.error             = error;
                ctx.state.statusMessage = vultra::trf("inspector.material.saveFailed", error);
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button((std::string {ICON_MDI_REFRESH " "} + vultra::tr("inspector.reload")).c_str()))
        {
            state.valid = readJsonFile(normalized, state.doc, state.error);
            state.dirty = false;
        }

        if (!state.error.empty())
            ImGui::TextWrapped("%s", state.error.c_str());
        return dirty;
    }

    bool InspectorWindow::drawMaterialGraphNodeSourceInspector(EditorContext& ctx, const std::filesystem::path& path)
    {
        struct NodeEditState
        {
            std::filesystem::path path;
            nlohmann::json        doc;
            std::string           error;
            bool                  valid {false};
            bool                  dirty {false};
        };

        static NodeEditState state;
        const auto normalized = path.lexically_normal();
        if (state.path != normalized)
        {
            state       = {};
            state.path  = normalized;
            state.valid = readJsonFile(normalized, state.doc, state.error);
            if (state.valid)
            {
                if (!state.doc.contains("type"))
                    state.doc["type"] = "MaterialGraphNode";
                if (!state.doc.contains("version"))
                    state.doc["version"] = 1;
                if (!state.doc.contains("inputs") || !state.doc["inputs"].is_array())
                    state.doc["inputs"] = nlohmann::json::array();
                if (!state.doc.contains("outputs") || !state.doc["outputs"].is_array())
                    state.doc["outputs"] = nlohmann::json::array({{{"name", "out"}, {"type", "float"}}});
                if (!state.doc.contains("defaultParams") || !state.doc["defaultParams"].is_object())
                    state.doc["defaultParams"] = nlohmann::json::object();
                if (!state.doc.contains("implementation") || !state.doc["implementation"].is_object())
                    state.doc["implementation"] = {{"language", "glsl"}, {"outputs", nlohmann::json::object()}};
                if (!state.doc["implementation"].contains("outputs") || !state.doc["implementation"]["outputs"].is_object())
                    state.doc["implementation"]["outputs"] = nlohmann::json::object();
            }
        }

        ui::sectionTitle(ICON_MDI_VECTOR_POINT, vultra::tr("inspector.graphNode.title"));
        if (!state.valid)
        {
            ImGui::TextWrapped("%s", vultra::trf("inspector.graphNode.parseFailed", state.error).c_str());
            if (ImGui::Button((std::string {ICON_MDI_REFRESH " "} + vultra::tr("inspector.reload")).c_str()))
            {
                state.valid = readJsonFile(normalized, state.doc, state.error);
                state.dirty = false;
            }
            return false;
        }

        bool dirty = false;
        auto typeId = state.doc.value("typeId", state.doc.value("id", std::string {"project.custom_node"}));
        if (drawMaterialStringInput(vultra::tr("inspector.graphNode.typeId"), typeId))
        {
            state.doc["typeId"] = typeId;
            dirty               = true;
        }
        auto displayName = state.doc.value("displayName", state.doc.value("name", std::string {"Custom Node"}));
        if (drawMaterialStringInput(vultra::tr("inspector.graphNode.displayName"), displayName))
        {
            state.doc["displayName"] = displayName;
            dirty                    = true;
        }

        auto drawPins = [&](const char* title, nlohmann::json& pins) {
            ImGui::Spacing();
            ImGui::TextUnformatted(title);
            int removeIndex = -1;
            for (int i = 0; i < static_cast<int>(pins.size()); ++i)
            {
                auto& pin = pins[static_cast<size_t>(i)];
                ImGui::PushID(i);
                if (ImGui::CollapsingHeader(pin.value("name", std::string {"pin"}).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                {
                    auto name = pin.value("name", std::string {});
                    if (drawMaterialStringInput(vultra::tr("common.name"), name))
                    {
                        pin["name"] = name;
                        dirty       = true;
                    }
                    auto type = vultra::material_graph::valueTypeFromString(pin.value("type", std::string {"float"}));
                    int  typeIndex = materialGraphValueTypeIndex(type);
                    ui::beginPropertyRow(vultra::tr("common.type"));
                    const bool typeChanged = ImGui::Combo("##Type",
                                                          &typeIndex,
                                                          kMaterialGraphValueTypeLabels,
                                                          IM_ARRAYSIZE(kMaterialGraphValueTypeLabels));
                    ui::endPropertyRow();
                    if (typeChanged)
                    {
                        type        = materialGraphValueTypeFromIndex(typeIndex);
                        pin["type"] = std::string(vultra::material_graph::toString(type));
                        if (pin.contains("defaultValue"))
                            pin["defaultValue"] = defaultMaterialGraphValue(type);
                        dirty = true;
                    }
                    auto defaultValue = pin.value("defaultValue", defaultMaterialGraphValue(type));
                    if (drawJsonDefaultValue(defaultValue, type))
                    {
                        pin["defaultValue"] = defaultValue;
                        dirty               = true;
                    }
                    if (ImGui::SmallButton((std::string {ICON_MDI_DELETE_OUTLINE " "} + vultra::tr("inspector.graphNode.removePin")).c_str()))
                        removeIndex = i;
                }
                ImGui::PopID();
            }
            if (removeIndex >= 0)
            {
                pins.erase(pins.begin() + removeIndex);
                dirty = true;
            }
            if (ImGui::SmallButton((std::string {ICON_MDI_PLUS " "} + vultra::tr("inspector.graphNode.addPin")).c_str()))
            {
                pins.push_back({{"name", "value"}, {"type", "float"}, {"defaultValue", 0.0f}});
                dirty = true;
            }
        };

        drawPins(vultra::tr("inspector.graphNode.inputs"), state.doc["inputs"]);
        drawPins(vultra::tr("inspector.graphNode.outputs"), state.doc["outputs"]);

        ImGui::Spacing();
        ImGui::TextUnformatted(vultra::tr("inspector.graphNode.defaultParams"));
        if (ImGui::BeginTable("NodeDefaultParams", 2, ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn(vultra::tr("common.name"), ImGuiTableColumnFlags_WidthFixed, vultra::ui::dp(160.0f));
            ImGui::TableSetupColumn(vultra::tr("common.value"), ImGuiTableColumnFlags_WidthStretch);
            std::string removeKey;
            for (auto& [key, value] : state.doc["defaultParams"].items())
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(key.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::PushID(key.c_str());
                if (value.is_number())
                {
                    float v = value.get<float>();
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::DragFloat("##value", &v, 0.01f))
                    {
                        value = v;
                        dirty = true;
                    }
                }
                else if (value.is_boolean())
                {
                    bool v = value.get<bool>();
                    if (ImGui::Checkbox("##value", &v))
                    {
                        value = v;
                        dirty = true;
                    }
                }
                else
                {
                    auto text = value.is_string() ? value.get<std::string>() : value.dump();
                    if (drawMaterialStringInput("##value", text))
                    {
                        value = text;
                        dirty = true;
                    }
                }
                if (ImGui::SmallButton(ICON_MDI_DELETE_OUTLINE "##remove"))
                    removeKey = key;
                ImGui::PopID();
            }
            if (!removeKey.empty())
            {
                state.doc["defaultParams"].erase(removeKey);
                dirty = true;
            }
            ImGui::EndTable();
        }
        static std::array<char, 64> newParamName {};
        ImGui::SetNextItemWidth(vultra::ui::dp(160.0f));
        ImGui::InputTextWithHint("##NewParamName", vultra::tr("inspector.graphNode.paramNameHint"), newParamName.data(), newParamName.size());
        ImGui::SameLine();
        if (ImGui::SmallButton((std::string {ICON_MDI_PLUS " "} + vultra::tr("inspector.graphNode.addParam")).c_str()) && newParamName[0] != '\0')
        {
            state.doc["defaultParams"][newParamName.data()] = 0.0f;
            newParamName = {};
            dirty        = true;
        }

        ImGui::Spacing();
        ImGui::TextUnformatted(vultra::tr("inspector.graphNode.glslOutputExpressions"));
        state.doc["implementation"]["language"] = "glsl";
        auto& outputs = state.doc["implementation"]["outputs"];
        for (const auto& pin : state.doc["outputs"])
        {
            const auto name = pin.value("name", std::string {});
            if (name.empty())
                continue;
            auto expression = outputs.value(name, std::string {});
            if (expression.empty())
                expression = "{{input:value}}";
            if (drawMaterialStringInput(name.c_str(), expression))
            {
                outputs[name] = expression;
                dirty         = true;
            }
        }

        const auto parsed = vultra::material_graph::nodeDescriptorFromJson(state.doc);
        if (!parsed.diagnostics.empty())
        {
            ImGui::Spacing();
            ImGui::TextUnformatted(vultra::tr("inspector.diagnostics"));
            for (const auto& diagnostic : parsed.diagnostics)
                ImGui::BulletText("%s", diagnostic.c_str());
        }

        state.dirty = state.dirty || dirty;
        ImGui::Spacing();
        ImGui::BeginDisabled(!state.dirty);
        if (ImGui::Button((std::string {ICON_MDI_CONTENT_SAVE " "} + vultra::tr("inspector.graphNode.save")).c_str()))
        {
            std::string error;
            if (writeJsonFile(state.path, state.doc, error))
            {
                state.dirty = false;
                state.error.clear();
                ++ctx.state.assetFileGeneration;
                if (auto* assets = ctx.services ? ctx.services->tryGet<vultra::IAssetService>() : nullptr)
                {
                    const auto assetRoot = editorAssetRoot(ctx);
                    std::error_code relEc;
                    const auto rel = std::filesystem::relative(state.path, assetRoot, relEc);
                    if (!relEc)
                        assets->clearTextAssetOverride("res://" + rel.generic_string());
                }
                ctx.state.statusMessage = vultra::trf("inspector.graphNode.saved", state.path.filename().generic_string());
            }
            else
            {
                state.error             = error;
                ctx.state.statusMessage = vultra::trf("inspector.graphNode.saveFailed", error);
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button((std::string {ICON_MDI_REFRESH " "} + vultra::tr("inspector.reload")).c_str()))
        {
            state.valid = readJsonFile(normalized, state.doc, state.error);
            state.dirty = false;
        }
        return dirty;
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

        ui::sectionTitle(ICON_MDI_VECTOR_POLYGON, vultra::tr("inspector.renderPass.title"));

        bool dirty = false;
        ui::beginPropertyRow(vultra::tr("common.type"));
        dirty |= ImGui::InputText("##Type", editState.type.data(), editState.type.size());
        ui::endPropertyRow();

        const std::string pipelines = std::string {vultra::tr("inspector.renderPass.pipeline.graphics")} + '\0' +
                                      vultra::tr("inspector.renderPass.pipeline.compute") + '\0' +
                                      vultra::tr("inspector.renderPass.pipeline.raytracing") + '\0';
        ui::beginPropertyRow(vultra::tr("inspector.renderPass.pipelineLabel"));
        dirty |= ImGui::Combo("##Pipeline", &editState.pipeline, pipelines.c_str());
        ui::endPropertyRow();

        ui::beginPropertyRow(vultra::tr("inspector.renderPass.inputs"));
        dirty |= ImGui::InputText("##Inputs", editState.inputs.data(), editState.inputs.size());
        ui::endPropertyRow();
        ui::beginPropertyRow(vultra::tr("inspector.renderPass.outputs"));
        dirty |= ImGui::InputText("##Outputs", editState.outputs.data(), editState.outputs.size());
        ui::endPropertyRow();

        if (editState.pipeline == 1)
        {
            dirty |= drawShaderLibrarySelector(vultra::tr("inspector.shader.library"), editState.library);
            dirty |= drawLibraryShaderSelector(ctx, vultra::tr("inspector.shader.compute"), "comp", editState.library, editState.compute);
            ui::beginPropertyRow(vultra::tr("inspector.renderPass.dispatchByOutputSize"));
            dirty |= ImGui::Checkbox("##DispatchByOutputSize", &editState.dispatchByOutputSize);
            ui::endPropertyRow();
        }
        else if (editState.pipeline == 2)
        {
            dirty |= drawShaderLibrarySelector(vultra::tr("inspector.shader.library"), editState.library);
            dirty |= drawLibraryShaderSelector(ctx, vultra::tr("inspector.shader.raygen"), "rgen", editState.library, editState.raygen);
            dirty |= drawLibraryShaderSelector(ctx, vultra::tr("inspector.shader.miss"), "rmiss", editState.library, editState.miss, true);
            dirty |= drawLibraryShaderSelector(ctx, vultra::tr("inspector.shader.closestHit"), "rchit", editState.library, editState.closestHit, true);
            dirty |= drawLibraryShaderSelector(ctx, vultra::tr("inspector.shader.anyHit"), "rahit", editState.library, editState.anyHit, true);
        }
        else
        {
            dirty |= drawShaderLibrarySelector(vultra::tr("inspector.shader.vertexLibrary"), editState.vertexLibrary);
            dirty |= drawShaderLibrarySelector(vultra::tr("inspector.shader.fragmentLibrary"), editState.fragmentLibrary);
            dirty |= normalizeGraphicsShaderSelection(ctx, editState);

            const bool builtinVertex = std::string_view(editState.vertexLibrary.data()) == "builtin" ||
                                       editState.vertexLibrary[0] == '\0';
            if (builtinVertex)
                dirty |= drawBuiltinFullscreenVertexField(editState.vertex);
            else
                dirty |= drawShaderSelector(ctx, vultra::tr("inspector.shader.vertex"), "vert", editState.vertex, false, false);

            if (std::string_view(editState.fragmentLibrary.data()) == "builtin")
                dirty |= drawShaderOptionSelector(vultra::tr("inspector.shader.fragment"), editState.fragment, collectBuiltinShaderIds("frag"));
            else
                dirty |= drawShaderSelector(ctx, vultra::tr("inspector.shader.fragment"), "frag", editState.fragment, false, false);
            dirty |= normalizeGraphicsShaderSelection(ctx, editState);
        }

        static bool pendingUnsaved = false;
        dirty |= normalizePipelineShaderSelection(ctx, editState);
        if (dirty)
            pendingUnsaved = true;

        ImGui::BeginDisabled(!pendingUnsaved);
        if (ImGui::Button((std::string {ICON_MDI_CONTENT_SAVE " "} + vultra::tr("inspector.renderPass.save")).c_str()))
        {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file.is_open())
            {
                ctx.state.statusMessage = vultra::tr("inspector.renderPass.saveFailedOpen");
            }
            else
            {
                static_cast<void>(normalizePipelineShaderSelection(ctx, editState));
                file << serializeRenderGraphPass(editState);
                file.close();
                if (!file)
                {
                    ctx.state.statusMessage = vultra::tr("inspector.renderPass.saveFailedWrite");
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
                    ctx.state.statusMessage = vultra::tr("inspector.renderPass.saved");
                }
            }
        }
        ImGui::EndDisabled();
        if (pendingUnsaved)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", vultra::tr("inspector.unsaved"));
        }

        return true;
    }

    void InspectorWindow::drawSourceTextureImportInspector(EditorContext& ctx, const std::filesystem::path& path)
    {
        const auto normalizedPath = path.lexically_normal();
        if (m_TextureImportEdit.path != normalizedPath || !m_TextureImportEdit.valid)
        {
            m_TextureImportEdit = {};
            m_TextureImportEdit.path = normalizedPath;
            auto sidecar = textureImportSidecarPath(normalizedPath);
            if (auto loaded = vasset::loadVImport(sidecar.generic_string()))
                m_TextureImportEdit.originalParams = loaded.value().params;

            m_TextureImportEdit.saved = vasset::resolveTextureImportParams(m_TextureImportEdit.originalParams);
            m_TextureImportEdit.edit  = m_TextureImportEdit.saved;
            m_TextureImportEdit.valid = true;
        }

        ui::sectionTitle(ICON_MDI_IMAGE, vultra::tr("inspector.textureImport.title"));

        auto& edit    = m_TextureImportEdit.edit;
        bool  changed = false;

        const auto importedPath = importedTexturePhysicalPath(ctx, normalizedPath);
        ImGui::TextUnformatted(
            vultra::trf("inspector.textureImport.sourceSize",
                        formatFileSizeIfPresent(normalizedPath, vultra::tr("inspector.textureImport.missing")))
                .c_str());
        ImGui::TextUnformatted(
            vultra::trf("inspector.textureImport.importedSize",
                        formatFileSizeIfPresent(importedPath, vultra::tr("inspector.textureImport.notImported")))
                .c_str());

        constexpr std::string_view subtypeIds[] = {
            vasset::kTextureSubtypeDefault,
            vasset::kTextureSubtypeUiSprite,
            vasset::kTextureSubtypeNormalMap,
            vasset::kTextureSubtypeCursor,
        };
        const auto subtypePreview = std::string(textureSubtypeLabel(edit.subtype));
        ui::beginPropertyRow(vultra::tr("inspector.textureImport.subtype"));
        if (ImGui::BeginCombo("##Subtype", subtypePreview.c_str()))
        {
            for (const auto subtype : subtypeIds)
            {
                const bool selected = edit.subtype == subtype;
                if (ImGui::Selectable(textureSubtypeLabel(subtype), selected))
                {
                    edit.subtype = std::string(subtype);
                    edit.options = vasset::textureImportOptionsForSubtype(edit.subtype);
                    changed      = true;
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ui::endPropertyRow();

        const auto checkboxRow = [&](const char* label, bool& field) {
            ui::beginPropertyRow(label);
            changed |= ImGui::Checkbox("##value", &field);
            ui::endPropertyRow();
        };

        checkboxRow(vultra::tr("inspector.textureImport.generateMipmaps"), edit.options.generateMipmaps);
        checkboxRow(vultra::tr("inspector.textureImport.flipY"), edit.options.flipY);
        changed |= drawTextureFileFormatCombo(edit.options.targetTextureFileFormat);
        checkboxRow("UASTC", edit.options.uastc);

        int qualityLevel = static_cast<int>(edit.options.qualityLevel);
        ui::beginPropertyRow(vultra::tr("inspector.textureImport.qualityLevel"));
        if (ImGui::SliderInt("##QualityLevel", &qualityLevel, 1, 255))
        {
            edit.options.qualityLevel = static_cast<uint32_t>(std::clamp(qualityLevel, 1, 255));
            changed = true;
        }
        ui::endPropertyRow();

        int compressionLevel = static_cast<int>(edit.options.compressionLevel);
        ui::beginPropertyRow(vultra::tr("inspector.textureImport.compressionLevel"));
        if (ImGui::SliderInt("##CompressionLevel", &compressionLevel, 0, 4))
        {
            edit.options.compressionLevel = static_cast<uint32_t>(std::clamp(compressionLevel, 0, 4));
            changed = true;
        }
        ui::endPropertyRow();

        checkboxRow(vultra::tr("inspector.textureImport.compressOnlyLarge"), edit.options.compressOnlyLargeTextures);
        checkboxRow(vultra::tr("inspector.textureImport.downscaleLarge"), edit.options.downscaleLargeTextures);

        int downscaleMin = static_cast<int>(edit.options.downscaleMinDimension);
        ui::beginPropertyRow(vultra::tr("inspector.textureImport.downscaleMinDimension"));
        if (ImGui::InputInt("##DownscaleMin", &downscaleMin))
        {
            edit.options.downscaleMinDimension = static_cast<uint32_t>(std::max(downscaleMin, 1));
            changed = true;
        }
        ui::endPropertyRow();

        int downscaleTarget = static_cast<int>(edit.options.downscaleTargetDimension);
        ui::beginPropertyRow(vultra::tr("inspector.textureImport.downscaleTargetDimension"));
        if (ImGui::InputInt("##DownscaleTarget", &downscaleTarget))
        {
            edit.options.downscaleTargetDimension = static_cast<uint32_t>(std::max(downscaleTarget, 1));
            changed = true;
        }
        ui::endPropertyRow();

        checkboxRow(vultra::tr("inspector.textureImport.bakeNormalMap"), edit.options.bakeNormalMap);
        checkboxRow(vultra::tr("inspector.textureImport.directXNormalMap"), edit.options.directXNormalMap);
        static_cast<void>(changed);

        const bool dirty = !textureImportParamsEqual(m_TextureImportEdit.saved, edit);
        ImGui::BeginDisabled(!dirty);
        if (ImGui::Button((std::string {ICON_MDI_CHECK " "} + vultra::tr("common.apply")).c_str()))
        {
            auto sidecar = textureImportSidecarPath(normalizedPath);
            vasset::VImport vimport {};
            if (auto loaded = vasset::loadVImport(sidecar.generic_string()))
                vimport = std::move(loaded.value());

            const auto relativeSource = relativeTextureSourcePath(ctx, normalizedPath);
            const auto importedPath   = vimport.output.empty() ? importedTexturePathForSource(ctx, relativeSource) : vimport.output;
            vimport.importer          = vasset::toString(vasset::VAssetType::eTexture);
            vimport.source            = relativeSource;
            vimport.output            = importedPath;
            if (!vimport.uid.valid())
                vimport.uid = vbase::uuid_from_string_key(importedPath);
            vimport.params =
                vasset::normalizedTextureImportParams(m_TextureImportEdit.originalParams, edit.subtype, edit.options);

            if (auto saved = vasset::saveVImport(vimport, sidecar.generic_string()); !saved)
            {
                ctx.state.statusMessage = vultra::tr("inspector.textureImport.saveFailed");
            }
            else
            {
                m_TextureImportEdit.originalParams = vimport.params;
                m_TextureImportEdit.saved          = vasset::resolveTextureImportParams(m_TextureImportEdit.originalParams);
                m_TextureImportEdit.edit           = m_TextureImportEdit.saved;
                queueTextureImport(ctx, normalizedPath, true);
                ctx.state.statusMessage =
                    vultra::trf("inspector.textureImport.applied", normalizedPath.filename().generic_string());
            }
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(!dirty);
        if (ImGui::Button((std::string {ICON_MDI_RESTORE " "} + vultra::tr("inspector.textureImport.revert")).c_str()))
        {
            auto sidecar = textureImportSidecarPath(normalizedPath);
            m_TextureImportEdit.originalParams.clear();
            if (auto loaded = vasset::loadVImport(sidecar.generic_string()))
                m_TextureImportEdit.originalParams = loaded.value().params;
            m_TextureImportEdit.saved = vasset::resolveTextureImportParams(m_TextureImportEdit.originalParams);
            m_TextureImportEdit.edit  = m_TextureImportEdit.saved;
        }
        ImGui::EndDisabled();
        if (dirty)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", vultra::tr("inspector.unsaved"));
        }
    }

    void InspectorWindow::drawSourceMeshImportInspector(EditorContext& ctx, const std::filesystem::path& path)
    {
        const auto normalizedPath = path.lexically_normal();
        if (m_MeshImportEdit.path != normalizedPath || !m_MeshImportEdit.valid)
        {
            m_MeshImportEdit      = {};
            m_MeshImportEdit.path = normalizedPath;
            auto sidecar          = textureImportSidecarPath(normalizedPath); // generic: <source>.vimport
            if (auto loaded = vasset::loadVImport(sidecar.generic_string()))
                m_MeshImportEdit.originalParams = loaded.value().params;
            m_MeshImportEdit.saved = vasset::resolveMeshImportParams(m_MeshImportEdit.originalParams);
            m_MeshImportEdit.edit  = m_MeshImportEdit.saved;
            m_MeshImportEdit.valid = true;
        }

        ui::sectionTitle(ICON_MDI_CUBE_OUTLINE, vultra::tr("inspector.meshImport.title"));

        auto&      edit       = m_MeshImportEdit.edit;
        const auto checkboxRow = [&](const char* label, bool& field) {
            ui::beginPropertyRow(label);
            ImGui::Checkbox("##value", &field);
            ui::endPropertyRow();
        };

        checkboxRow(vultra::tr("inspector.meshImport.calcTangentSpace"), edit.calcTangentSpace);
        checkboxRow(vultra::tr("inspector.meshImport.genSmoothNormals"), edit.genSmoothNormals);
        checkboxRow(vultra::tr("inspector.meshImport.genUVCoords"), edit.genUVCoords);
        checkboxRow(vultra::tr("inspector.meshImport.flipUVs"), edit.flipUVs);
        checkboxRow(vultra::tr("inspector.meshImport.preTransformVertices"), edit.preTransformVertices);
        checkboxRow(vultra::tr("inspector.meshImport.generateMeshlets"), edit.generateMeshlets);

        const auto& s     = m_MeshImportEdit.saved;
        const bool  dirty = s.calcTangentSpace != edit.calcTangentSpace || s.genSmoothNormals != edit.genSmoothNormals ||
                           s.genUVCoords != edit.genUVCoords || s.flipUVs != edit.flipUVs ||
                           s.preTransformVertices != edit.preTransformVertices ||
                           s.generateMeshlets != edit.generateMeshlets;

        ImGui::BeginDisabled(!dirty);
        if (ImGui::Button((std::string {ICON_MDI_CHECK " "} + vultra::tr("common.apply")).c_str()))
        {
            auto            sidecar = textureImportSidecarPath(normalizedPath);
            vasset::VImport vimport {};
            if (auto loaded = vasset::loadVImport(sidecar.generic_string()))
                vimport = std::move(loaded.value()); // preserve importer/source/output/uid from the cook
            vimport.params = vasset::normalizedMeshImportParams(m_MeshImportEdit.originalParams, edit);

            if (auto saved = vasset::saveVImport(vimport, sidecar.generic_string()); !saved)
            {
                ctx.state.statusMessage = vultra::tr("inspector.meshImport.saveFailed");
            }
            else
            {
                m_MeshImportEdit.originalParams = vimport.params;
                m_MeshImportEdit.saved          = vasset::resolveMeshImportParams(m_MeshImportEdit.originalParams);
                m_MeshImportEdit.edit           = m_MeshImportEdit.saved;
                queueTextureImport(ctx, normalizedPath, true); // generic reimport queue
                ctx.state.statusMessage =
                    vultra::trf("inspector.meshImport.applied", normalizedPath.filename().generic_string());
            }
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(!dirty);
        if (ImGui::Button((std::string {ICON_MDI_RESTORE " "} + vultra::tr("inspector.meshImport.revert")).c_str()))
        {
            auto sidecar = textureImportSidecarPath(normalizedPath);
            m_MeshImportEdit.originalParams.clear();
            if (auto loaded = vasset::loadVImport(sidecar.generic_string()))
                m_MeshImportEdit.originalParams = loaded.value().params;
            m_MeshImportEdit.saved = vasset::resolveMeshImportParams(m_MeshImportEdit.originalParams);
            m_MeshImportEdit.edit  = m_MeshImportEdit.saved;
        }
        ImGui::EndDisabled();
        if (dirty)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", vultra::tr("inspector.unsaved"));
        }
    }

    void InspectorWindow::drawSourceAudioImportInspector(EditorContext& ctx, const std::filesystem::path& path)
    {
        const auto normalizedPath = path.lexically_normal();
        const auto sourceExt      = normalizedPath.extension().generic_string();
        if (m_AudioImportEdit.path != normalizedPath || !m_AudioImportEdit.valid)
        {
            m_AudioImportEdit      = {};
            m_AudioImportEdit.path = normalizedPath;
            auto sidecar           = textureImportSidecarPath(normalizedPath); // generic: <source>.vimport
            if (auto loaded = vasset::loadVImport(sidecar.generic_string()))
                m_AudioImportEdit.originalParams = loaded.value().params;
            m_AudioImportEdit.saved = vasset::resolveAudioImportParams(m_AudioImportEdit.originalParams, sourceExt);
            m_AudioImportEdit.edit  = m_AudioImportEdit.saved;
            m_AudioImportEdit.valid = true;
        }

        ui::sectionTitle(ICON_MDI_VOLUME_HIGH, vultra::tr("inspector.audioImport.title"));

        auto& edit = m_AudioImportEdit.edit;

        // Subtype is keyed by the source format and therefore fixed per file.
        ui::beginPropertyRow(vultra::tr("inspector.audioImport.subtype"));
        ImGui::TextUnformatted(edit.subtype.c_str());
        ui::endPropertyRow();

        const bool  isOgg = sourceExt == ".ogg";
        const char* storageLabels[] = {
            vultra::tr("inspector.audioImport.storagePcm16"),
            vultra::tr("inspector.audioImport.storagePcmF32"),
            vultra::tr("inspector.audioImport.storagePassthrough"),
        };
        int storageIndex = vasset::isPassthrough(edit.options.storage) ? 2 :
                           edit.options.storage == vasset::VAudioStorage::ePCMF32 ? 1 : 0;
        ui::beginPropertyRow(vultra::tr("inspector.audioImport.storage"));
        if (ImGui::BeginCombo("##Storage", storageLabels[storageIndex]))
        {
            // Ogg cannot passthrough: the runtime has no vorbis decoder.
            const int storageCount = isOgg ? 2 : 3;
            for (int i = 0; i < storageCount; ++i)
            {
                const bool selected = storageIndex == i;
                if (ImGui::Selectable(storageLabels[i], selected))
                {
                    edit.options.storage = i == 0 ? vasset::VAudioStorage::ePCM16 :
                                           i == 1 ? vasset::VAudioStorage::ePCMF32 :
                                                    vasset::VAudioStorage::ePassthroughWav;
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ui::endPropertyRow();

        const bool pcmStorage = !vasset::isPassthrough(edit.options.storage);
        ImGui::BeginDisabled(!pcmStorage);
        int sampleRate = static_cast<int>(edit.options.targetSampleRate);
        ui::beginPropertyRow(vultra::tr("inspector.audioImport.targetSampleRate"));
        if (ImGui::InputInt("##TargetSampleRate", &sampleRate))
            edit.options.targetSampleRate = static_cast<uint32_t>(std::max(sampleRate, 0));
        ui::endPropertyRow();

        ui::beginPropertyRow(vultra::tr("inspector.audioImport.forceMono"));
        ImGui::Checkbox("##ForceMono", &edit.options.forceMono);
        ui::endPropertyRow();

        ui::beginPropertyRow(vultra::tr("inspector.audioImport.normalize"));
        ImGui::Checkbox("##Normalize", &edit.options.normalize);
        ui::endPropertyRow();
        ImGui::EndDisabled();

        // Reserved for a future lossy encoder (Vorbis/Opus); persisted but inactive in v1.
        ImGui::BeginDisabled(true);
        int bitrate = static_cast<int>(edit.options.bitrateKbps);
        ui::beginPropertyRow(vultra::tr("inspector.audioImport.bitrateKbps"));
        ImGui::InputInt("##BitrateKbps", &bitrate);
        ui::endPropertyRow();
        int quality = static_cast<int>(edit.options.quality);
        ui::beginPropertyRow(vultra::tr("inspector.audioImport.quality"));
        ImGui::InputInt("##Quality", &quality);
        ui::endPropertyRow();
        ImGui::EndDisabled();
        ImGui::TextDisabled("%s", vultra::tr("inspector.audioImport.encoderNote"));

        const auto& s     = m_AudioImportEdit.saved;
        const bool  dirty = vasset::audioStorageToParam(s.options.storage) !=
                               vasset::audioStorageToParam(edit.options.storage) ||
                           s.options.targetSampleRate != edit.options.targetSampleRate ||
                           s.options.forceMono != edit.options.forceMono ||
                           s.options.normalize != edit.options.normalize;

        ImGui::BeginDisabled(!dirty);
        if (ImGui::Button((std::string {ICON_MDI_CHECK " "} + vultra::tr("common.apply")).c_str()))
        {
            auto            sidecar = textureImportSidecarPath(normalizedPath);
            vasset::VImport vimport {};
            if (auto loaded = vasset::loadVImport(sidecar.generic_string()))
                vimport = std::move(loaded.value()); // preserve importer/source/output/uid from the cook
            vimport.params =
                vasset::normalizedAudioImportParams(m_AudioImportEdit.originalParams, edit.subtype, edit.options);

            if (auto saved = vasset::saveVImport(vimport, sidecar.generic_string()); !saved)
            {
                ctx.state.statusMessage = vultra::tr("inspector.audioImport.saveFailed");
            }
            else
            {
                m_AudioImportEdit.originalParams = vimport.params;
                m_AudioImportEdit.saved =
                    vasset::resolveAudioImportParams(m_AudioImportEdit.originalParams, sourceExt);
                m_AudioImportEdit.edit = m_AudioImportEdit.saved;
                queueTextureImport(ctx, normalizedPath, true); // generic reimport queue
                ctx.state.statusMessage =
                    vultra::trf("inspector.audioImport.applied", normalizedPath.filename().generic_string());
            }
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(!dirty);
        if (ImGui::Button((std::string {ICON_MDI_RESTORE " "} + vultra::tr("inspector.audioImport.revert")).c_str()))
        {
            auto sidecar = textureImportSidecarPath(normalizedPath);
            m_AudioImportEdit.originalParams.clear();
            if (auto loaded = vasset::loadVImport(sidecar.generic_string()))
                m_AudioImportEdit.originalParams = loaded.value().params;
            m_AudioImportEdit.saved = vasset::resolveAudioImportParams(m_AudioImportEdit.originalParams, sourceExt);
            m_AudioImportEdit.edit  = m_AudioImportEdit.saved;
        }
        ImGui::EndDisabled();
        if (dirty)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", vultra::tr("inspector.unsaved"));
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
        ImGui::TextUnformatted(vultra::tr("inspector.preview"));
        const float size = std::min(ImGui::GetContentRegionAvail().x, vultra::ui::dp(260.0f));
        ImGui::Image(previewId, ImVec2(size, size));
        (void)ui::capturePreviewItemInput();
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
        ImGui::TextUnformatted(vultra::tr("inspector.preview"));
        const float    width        = std::clamp(ImGui::GetContentRegionAvail().x, vultra::ui::dp(140.0f), vultra::ui::dp(220.0f));
        const float    height       = std::clamp(width * 0.68f, vultra::ui::dp(120.0f), vultra::ui::dp(180.0f));
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
            ImGui::TextDisabled("%s", vultra::tr("inspector.modelPreview.servicesUnavailable"));
            return;
        }

        (void)worldService;

        ImGui::Image(m_ModelPreviewTarget.textureId, ImVec2(width, height));
        const bool   hovered  = ui::capturePreviewItemInput();
        const ImVec2 imageMin = ImGui::GetItemRectMin();
        const ImVec2 imageMax = ImGui::GetItemRectMax();
        ui::capturePreviewInput(m_ModelPreviewArcballActive);

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
        drawList->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(72, 150, 225, 180), vultra::ui::dp(4.0f));

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
        if (m_ModelPreviewAnimated)
        {
            if (auto* animationService = ctx.services->tryGet<vultra::IAnimationService>())
            {
                const float dt = m_ModelPreviewAnimationPlaying ? std::max(ImGui::GetIO().DeltaTime, 0.0f) : 0.0f;
                animationService->updateWorld(m_ModelPreviewWorld, fsec {dt});
                m_ModelPreviewDirty = true;
            }
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
            ImGui::TextDisabled("%s", vultra::tr("inspector.modelPreview.empty"));
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
        camera.allowUpscaler           = false;
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

        const auto frame                  = static_cast<uint64_t>(ImGui::GetFrameCount());
        auto*      imguiServiceForCleanup = ctx.services->tryGet<vultra::IImGuiService>();
        m_RetiredModelPreviewTargets.reclaim(imguiServiceForCleanup, frame);

        if (m_ModelPreviewTarget.texture && m_ModelPreviewTarget.extent.width == width &&
            m_ModelPreviewTarget.extent.height == height && m_ModelPreviewTarget.textureId)
        {
            return;
        }

        m_RetiredModelPreviewTargets.retire(m_ModelPreviewTarget, frame, kModelPreviewTargetReleaseDelayFrames);

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
                m_RetiredModelPreviewTargets.releaseAll(imguiService);
            }
        }
        m_ModelPreviewTarget = {};
        m_RetiredModelPreviewTargets.releaseAll(ctx.services ? ctx.services->tryGet<vultra::IImGuiService>() : nullptr);
        m_ModelPreviewDirty           = true;
        m_ModelPreviewCameraSubmitted = false;
        m_ModelPreviewAnimated        = false;
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
        m_ModelPreviewAnimated        = false;
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
        m_ModelPreviewAnimated        = false;
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

    void InspectorWindow::rebuildModelPreviewWorldForAnimation(
        EditorContext& ctx, const vultra::CoreUUID& uuid, const vasset::VAssetRegistry::AssetEntry& entry)
    {
        if (ctx.services)
        {
            if (auto* renderService = ctx.services->tryGet<vultra::IRenderService>())
                renderService->releaseOverrideRenderWorld(&m_ModelPreviewWorld);
        }

        m_ModelPreviewWorld.clear();
        m_ModelPreviewRoot        = entt::null;
        m_ModelPreviewContentRoot = entt::null;
        m_ModelPreviewKey         = "animation:" + uuid.toString() + ":" + entry.importedPath;
        m_ModelPreviewPath.clear();
        m_ModelPreviewRotation        = glm::quat {1.0f, 0.0f, 0.0f, 0.0f};
        m_ModelPreviewArcballVector   = glm::vec3 {0.0f, 0.0f, 1.0f};
        m_ModelPreviewArcballActive   = false;
        m_ModelPreviewCameraSubmitted = false;
        m_ModelPreviewDirty           = true;
        m_ModelPreviewAnimated        = true;
        m_ModelPreviewAnimationPlaying = true;
        m_ModelPreviewAnimationLoop    = true;
        m_ModelPreviewAnimationSpeed   = 1.0f;
        m_ModelPreviewDistanceScale    = 1.0f;

        addPreviewLighting(m_ModelPreviewWorld);
        m_ModelPreviewRoot = m_ModelPreviewWorld.createEntity();
        m_ModelPreviewWorld.registry().emplace<vultra::NameComponent>(m_ModelPreviewRoot,
                                                                      vultra::NameComponent {"Preview Animation Pivot"});
        m_ModelPreviewContentRoot = m_ModelPreviewWorld.createChild(m_ModelPreviewRoot);
        m_ModelPreviewWorld.registry().emplace<vultra::NameComponent>(
            m_ModelPreviewContentRoot, vultra::NameComponent {"Preview Animation Content"});
        if (!ctx.services)
            return;

        auto* assetService = ctx.services->tryGet<vultra::IAssetService>();
        auto* sceneService = ctx.services->tryGet<vultra::ISceneService>();
        if (!assetService || !sceneService)
            return;

        const std::string sourcePrefix = sourcePrefixBeforeSubAsset(entry.sourcePath);
        vultra::CoreUUID  skeletonUuid {};
        vultra::CoreUUID  meshUuid {};
        std::string       manifestUri;
        for (const auto& [entryUuid, candidate] : assetService->registry().getRegistry())
        {
            if (candidate.sourcePath.empty())
                continue;

            if (candidate.type == vasset::VAssetType::eSceneManifest && candidate.sourcePath == sourcePrefix &&
                !candidate.importedPath.empty())
            {
                manifestUri = "res://" + candidate.importedPath;
                continue;
            }

            if (candidate.type == vasset::VAssetType::eSkeleton && candidate.sourcePath == sourcePrefix + "#skeleton")
            {
                skeletonUuid = coreUuidFromRegistryKey(entryUuid);
                continue;
            }

            if (candidate.type != vasset::VAssetType::eMesh ||
                !candidate.sourcePath.starts_with(sourcePrefix + "#mesh/"))
            {
                continue;
            }

            const auto candidateMeshUuid = coreUuidFromRegistryKey(entryUuid);
            if (!candidateMeshUuid.valid())
                continue;

            auto meshHandle = assetService->loadMeshSync(candidateMeshUuid);
            if (!meshHandle.ready() || !meshHandle.cpu() || !meshHandle.cpu()->hasSkin)
                continue;

            meshUuid = candidateMeshUuid;
            if (!skeletonUuid.valid())
                skeletonUuid = vultra::CoreUUID(meshHandle.cpu()->skeleton);
        }

        if (!manifestUri.empty())
        {
            (void)sceneService->instantiateScene(m_ModelPreviewWorld, manifestUri, m_ModelPreviewContentRoot, false);
        }
        else if (meshUuid.valid())
        {
            auto entity = m_ModelPreviewWorld.createChild(m_ModelPreviewContentRoot);
            auto& reg   = m_ModelPreviewWorld.registry();
            reg.emplace<vultra::NameComponent>(entity, vultra::NameComponent {"Animation Preview Mesh"});
            reg.emplace<vultra::MeshComponent>(entity, vultra::MeshComponent {.mesh = meshUuid});
        }

        auto& reg = m_ModelPreviewWorld.registry();
        bool  configuredAnimator = false;
        auto  animatorView       = reg.view<vultra::AnimatorComponent>();
        for (auto entity : animatorView)
        {
            auto& animator     = animatorView.get<vultra::AnimatorComponent>(entity);
            animator.skeleton  = skeletonUuid;
            animator.animation = uuid;
            animator.playing   = m_ModelPreviewAnimationPlaying;
            animator.playOnStart = false;
            animator.loop      = m_ModelPreviewAnimationLoop;
            animator.speed     = m_ModelPreviewAnimationSpeed;
            animator.time      = 0.0f;
            configuredAnimator = true;
        }
        if (!configuredAnimator && skeletonUuid.valid())
        {
            reg.emplace<vultra::AnimatorComponent>(m_ModelPreviewContentRoot,
                                                   vultra::AnimatorComponent {
                                                       .skeleton = skeletonUuid,
                                                       .animation = uuid,
                                                       .playOnStart = false,
                                                       .playing = m_ModelPreviewAnimationPlaying,
                                                       .loop = m_ModelPreviewAnimationLoop,
                                                       .speed = m_ModelPreviewAnimationSpeed,
                                                       .time = 0.0f,
                                                   });
        }

        centerPreviewContent(m_ModelPreviewWorld, *assetService, m_ModelPreviewContentRoot);
    }
} // namespace vultra_app
