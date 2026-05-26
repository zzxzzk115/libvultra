#include "editor_app/asset_thumbnail_service.hpp"

#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/camera_service.hpp>
#include <vultra/function/services/render_backend_service.hpp>
#include <vultra/function/services/world_service.hpp>
#include <vultra/function/services/scene_service.hpp>
#include <vultra/core/services/window_service.hpp>
#include <vultra/function/rendering/render_structs.hpp>
#include <vultra/function/world/components/environment_component.hpp>
#include <vultra/function/world/components/light_component.hpp>
#include <vultra/function/world/components/mesh_component.hpp>
#include <vultra/function/world/components/name_component.hpp>
#include <vultra/function/world/components/transform_component.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <system_error>
#include <vector>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/geometric.hpp>

#include <stb_image.h>
#include <stb_image_resize2.h>
#include <stb_image_write.h>

namespace vultra_app::ui
{
    namespace
    {
        constexpr std::string_view kThumbnailCacheVersion = "lighting-v2";
        constexpr std::string_view kTextureThumbnailCacheVersion = "texture-v1";
        constexpr int              kTextureThumbnailSize = 128;

        std::string fnv1a64Hex(std::string_view text)
        {
            uint64_t hash = 14695981039346656037ull;
            for (const unsigned char ch : text)
            {
                hash ^= ch;
                hash *= 1099511628211ull;
            }

            std::ostringstream out;
            out << std::hex << std::setw(16) << std::setfill('0') << hash;
            return out.str();
        }

        std::string normalizedGeneric(std::filesystem::path path)
        {
            auto text = path.lexically_normal().generic_string();
            std::replace(text.begin(), text.end(), '\\', '/');
            return text;
        }

        uint64_t fileWriteStamp(const std::filesystem::path& path)
        {
            std::error_code ec;
            const auto time = std::filesystem::last_write_time(path, ec);
            if (ec)
                return 0;
            return static_cast<uint64_t>(time.time_since_epoch().count());
        }

        bool isModelSourcePath(const std::string& path)
        {
            auto ext = std::filesystem::path(path).extension().generic_string();
            std::transform(ext.begin(),
                           ext.end(),
                           ext.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return ext == ".gltf" || ext == ".glb" || ext == ".obj" || ext == ".fbx" || ext == ".dae";
        }

        bool isTextureSourcePath(const std::string& path)
        {
            auto ext = std::filesystem::path(path).extension().generic_string();
            std::transform(ext.begin(),
                           ext.end(),
                           ext.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" ||
                   ext == ".hdr";
        }

        glm::mat4 localMatrix(const vultra::TransformComponent& t)
        {
            return glm::translate(glm::mat4 {1.0f}, t.position) * glm::mat4_cast(t.rotation) *
                   glm::scale(glm::mat4 {1.0f}, t.scale);
        }

        glm::mat4 worldMatrix(const vultra::World& world, const entt::entity entity)
        {
            const auto& reg = world.registry();
            const auto* transform = reg.try_get<vultra::TransformComponent>(entity);
            if (!transform)
                return glm::mat4 {1.0f};

            auto matrix = localMatrix(*transform);
            const auto parent = world.parent(entity);
            if (parent != entt::null && reg.valid(parent))
                matrix = worldMatrix(world, parent) * matrix;
            return matrix;
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

            void includeTransformed(const glm::vec3& p, const glm::mat4& m)
            {
                include(glm::vec3(m * glm::vec4(p, 1.0f)));
            }
        };

        void includeMeshBounds(Bounds& bounds, const vasset::VMesh& mesh, const glm::mat4& matrix)
        {
            for (const auto& p : mesh.positions)
                bounds.includeTransformed(glm::vec3 {p.x, p.y, p.z}, matrix);
        }

        Bounds computeWorldMeshBounds(vultra::World& world, vultra::IAssetService& assets)
        {
            Bounds bounds;
            auto&  reg = world.registry();
            auto   view = reg.view<vultra::TransformComponent, vultra::MeshComponent>();
            for (auto entity : view)
            {
                const auto& meshComponent = view.get<vultra::MeshComponent>(entity);
                if (!meshComponent.mesh.valid())
                    continue;

                auto handle = assets.loadMeshSync(meshComponent.mesh);
                if (!handle.ready() || !handle.cpu())
                    continue;

                includeMeshBounds(bounds, *handle.cpu(), worldMatrix(world, entity));
            }
            return bounds;
        }

        void addPreviewLighting(vultra::World& world)
        {
            auto& reg = world.registry();

            auto light = world.createEntity();
            reg.emplace<vultra::NameComponent>(light, vultra::NameComponent {"Thumbnail Light"});
            auto& lightTransform = reg.get<vultra::TransformComponent>(light);
            lightTransform.rotation = glm::quatLookAt(glm::normalize(glm::vec3 {0.4f, -0.55f, 0.7f}),
                                                       glm::vec3 {0.0f, 1.0f, 0.0f});
            reg.emplace<vultra::LightComponent>(light,
                                                vultra::LightComponent {
                                                    .kind = 0u,
                                                    .color = glm::vec3 {1.0f},
                                                    .intensity = 6.0f,
                                                    .castsShadow = false,
                                                });

            auto env = world.createEntity();
            reg.emplace<vultra::NameComponent>(env, vultra::NameComponent {"Thumbnail Environment"});
            reg.emplace<vultra::EnvironmentComponent>(env,
                                                      vultra::EnvironmentComponent {
                                                          .ambientColor = glm::vec3 {0.28f, 0.30f, 0.34f},
                                                          .ambientIntensity = 1.8f,
                                                          .enableIBL = false,
                                                      });
        }

        void setPreviewLightDirection(vultra::World& world, glm::vec3 direction)
        {
            auto& reg = world.registry();
            auto  view = reg.view<vultra::NameComponent, vultra::TransformComponent>();
            entt::entity lightEntity = entt::null;
            for (auto entity : view)
            {
                if (view.get<vultra::NameComponent>(entity).name == "Thumbnail Light")
                {
                    lightEntity = entity;
                    break;
                }
            }
            if (lightEntity == entt::null)
                return;

            const float len2 = glm::dot(direction, direction);
            direction = len2 > 1e-8f ? direction * glm::inversesqrt(len2) : glm::vec3 {0.0f, -1.0f, 0.0f};
            glm::vec3 up {0.0f, 1.0f, 0.0f};
            if (std::abs(glm::dot(up, direction)) > 0.95f)
                up = glm::vec3 {1.0f, 0.0f, 0.0f};

            auto& transform = view.get<vultra::TransformComponent>(lightEntity);
            transform.rotation = glm::normalize(glm::quatLookAtRH(direction, up));
            transform.dirty = true;
        }

        vultra::RenderCamera makePreviewCamera(const Bounds& bounds, vultra::rhi::Texture* target)
        {
            const glm::vec3 center = bounds.valid ? (bounds.min + bounds.max) * 0.5f : glm::vec3 {0.0f};
            const glm::vec3 size = bounds.valid ? (bounds.max - bounds.min) : glm::vec3 {1.0f};
            float maxDimension = std::max({size.x, size.y, size.z, 1.0f});

            const float fovY = glm::radians(45.0f);
            float distance = (maxDimension * 0.65f) / std::tan(fovY * 0.5f);
            distance = std::max(distance, maxDimension * 1.6f);

            glm::vec3 offset = glm::normalize(glm::vec3 {0.5f, 0.32f, 0.62f}) * distance;
            const float radius = std::max(0.5f, maxDimension * 0.5f);

            vultra::RenderCamera camera {};
            camera.name = "Thumbnail Camera";
            camera.view = glm::lookAt(center + offset, center, glm::vec3 {0.0f, 1.0f, 0.0f});
            camera.projection = glm::perspectiveRH_ZO(fovY, 1.0f, std::max(0.01f, distance - radius * 3.0f),
                                                      std::max(distance + radius * 6.0f, 1000.0f));
            camera.target = target;
            camera.fovY = fovY;
            camera.zNear = std::max(0.01f, distance - radius * 3.0f);
            camera.zFar = std::max(distance + radius * 6.0f, 1000.0f);
            camera.clearValue = glm::vec4 {0.06f, 0.07f, 0.08f, 1.0f};
            camera.clearMode = 0u;
            camera.renderImGui = false;
            camera.rendererKey = "universal";
            return camera;
        }

        void addLoadingShellCamera(vbase::ServiceRegistry& services)
        {
            auto* cameraService = services.tryGet<vultra::ICameraService>();
            auto* windowService = services.tryGet<IWindowService>();
            if (!cameraService || !windowService)
                return;

            const auto extent = windowService->window().getExtent();
            const float width = static_cast<float>(std::max(extent.x, 1));
            const float height = static_cast<float>(std::max(extent.y, 1));

            vultra::RenderCamera shellCamera {};
            shellCamera.name = "Vultra Editor UI";
            shellCamera.priority = 1000;
            shellCamera.view = glm::lookAt(glm::vec3 {0.0f, 0.0f, 1.0f},
                                           glm::vec3 {0.0f, 0.0f, 0.0f},
                                           glm::vec3 {0.0f, 1.0f, 0.0f});
            shellCamera.projection = glm::perspectiveRH_ZO(glm::radians(60.0f), width / height, 0.1f, 1000.0f);
            shellCamera.zNear = 0.1f;
            shellCamera.zFar = 1000.0f;
            shellCamera.fovY = glm::radians(60.0f);
            shellCamera.clearValue = {0.018f, 0.02f, 0.026f, 1.0f};
            shellCamera.renderImGui = true;
            shellCamera.rendererKey = "editor-shell";
            cameraService->addManualCamera(shellCamera);
        }

        bool parseUuid(std::string_view text, vultra::CoreUUID& out)
        {
            vbase::UUID parsed {};
            if (!vbase::try_parse_uuid(std::string(text).c_str(), parsed))
                return false;
            out = vultra::CoreUUID(parsed);
            return true;
        }

        std::string trim(std::string text)
        {
            const auto first = std::find_if_not(text.begin(), text.end(), [](unsigned char ch) {
                return std::isspace(ch) != 0;
            });
            const auto last = std::find_if_not(text.rbegin(), text.rend(), [](unsigned char ch) {
                return std::isspace(ch) != 0;
            }).base();
            if (first >= last)
                return {};
            return std::string(first, last);
        }

        std::string unquote(std::string text)
        {
            text = trim(std::move(text));
            if (text.size() >= 2 && text.front() == '"' && text.back() == '"')
                return text.substr(1, text.size() - 2);
            return text;
        }

        bool parseFloatTuple(std::string text, std::vector<float>& out)
        {
            out.clear();
            text = trim(std::move(text));
            if (text.size() < 2 || text.front() != '(' || text.back() != ')')
                return false;

            std::stringstream ss(text.substr(1, text.size() - 2));
            std::string       part;
            while (std::getline(ss, part, ','))
            {
                try
                {
                    out.push_back(std::stof(trim(part)));
                }
                catch (...)
                {
                    out.clear();
                    return false;
                }
            }
            return !out.empty();
        }

        struct MeshSubAssetPlacement
        {
            vultra::TransformComponent transform;
            std::filesystem::path      manifestPath;
        };

        std::optional<MeshSubAssetPlacement>
        findMeshSubAssetPlacement(const vasset::VAssetRegistry& registry,
                                  const std::filesystem::path&  assetRoot,
                                  const std::string&            meshImportedPath)
        {
            if (meshImportedPath.empty())
                return std::nullopt;

            const auto meshUri = "res://" + meshImportedPath;
            for (const auto& [uuid, entry] : registry.getRegistry())
            {
                (void)uuid;
                if (entry.type != vasset::VAssetType::eSceneManifest || entry.importedPath.empty())
                    continue;

                const auto manifestPath = assetRoot / std::filesystem::path(entry.importedPath);
                std::ifstream in(manifestPath);
                if (!in)
                    continue;

                MeshSubAssetPlacement current {};
                current.manifestPath = manifestPath;

                bool        inNode = false;
                std::string line;
                while (std::getline(in, line))
                {
                    line = trim(line);
                    if (line.empty())
                        continue;

                    if (line.starts_with("[node "))
                    {
                        current = MeshSubAssetPlacement {};
                        current.manifestPath = manifestPath;
                        inNode               = true;
                        continue;
                    }

                    if (!inNode)
                        continue;

                    const auto equals = line.find('=');
                    if (equals == std::string::npos)
                        continue;

                    const auto key   = trim(line.substr(0, equals));
                    const auto value = trim(line.substr(equals + 1));
                    std::vector<float> tuple;
                    if (key == "TransformComponent/position" && parseFloatTuple(value, tuple) && tuple.size() == 3)
                    {
                        current.transform.position = glm::vec3 {tuple[0], tuple[1], tuple[2]};
                    }
                    else if (key == "TransformComponent/rotation" && parseFloatTuple(value, tuple) && tuple.size() == 4)
                    {
                        current.transform.rotation = glm::normalize(glm::quat {tuple[3], tuple[0], tuple[1], tuple[2]});
                    }
                    else if (key == "TransformComponent/scale" && parseFloatTuple(value, tuple) && tuple.size() == 3)
                    {
                        current.transform.scale = glm::vec3 {tuple[0], tuple[1], tuple[2]};
                    }
                    else if (key == "MeshComponent/mesh" && unquote(value) == meshUri)
                    {
                        current.transform.dirty = true;
                        return current;
                    }
                }
            }

            return std::nullopt;
        }
    } // namespace

    void AssetThumbnailService::syncProject(EditorContext& ctx)
    {
        const auto projectRoot = ctx.state.currentProject.lexically_normal();
        if (projectRoot == m_ProjectRoot && ctx.state.currentAssetRoot == m_AssetRootName &&
            ctx.state.projectGeneration == m_ProjectGeneration)
        {
            return;
        }

        m_ProjectRoot        = projectRoot;
        m_AssetRootName      = ctx.state.currentAssetRoot;
        m_ProjectGeneration  = ctx.state.projectGeneration;
        m_CacheRoot          = m_ProjectRoot.empty() ? std::filesystem::path {} : m_ProjectRoot / ".vultra" / "thumbs";
        m_StatusCache.clear();
        m_ModelRootRequestCache.clear();
        m_MeshRequestCache.clear();
        m_TextureRequestCache.clear();
        m_QueuedRequests.clear();
        m_ActiveRenderJob.reset();
        m_TotalQueuedThisPass = 0;
    }

    void AssetThumbnailService::clear()
    {
        m_ProjectRoot.clear();
        m_AssetRootName.clear();
        m_CacheRoot.clear();
        m_ProjectGeneration = 0;
        m_StatusCache.clear();
        m_ModelRootRequestCache.clear();
        m_MeshRequestCache.clear();
        m_TextureRequestCache.clear();
        m_QueuedRequests.clear();
        m_ActiveRenderJob.reset();
        m_TotalQueuedThisPass = 0;
        m_FrameCounter = 0;
    }

    AssetThumbnailRequest AssetThumbnailService::requestModelRoot(EditorContext& ctx,
                                                                  const std::filesystem::path& sourcePath)
    {
        syncProject(ctx);

        const std::string cacheKey = sourcePath.lexically_normal().generic_string();
        if (auto cachedIt = m_ModelRootRequestCache.find(cacheKey); cachedIt != m_ModelRootRequestCache.end())
        {
            auto request = cachedIt->second;
            if (auto statusIt = m_StatusCache.find(request.key); statusIt != m_StatusCache.end())
                request.status = statusIt->second;
            return request;
        }

        AssetThumbnailRequest request;
        request.kind       = AssetThumbnailKind::ModelRoot;
        request.sourcePath = sourcePath.lexically_normal();
        request.sourceUri  = sourceUriFor(ctx, request.sourcePath);

        if (ctx.services)
        {
            if (auto* assetService = ctx.services->tryGet<vultra::IAssetService>())
            {
                const auto assetRoot = projectAssetRoot(ctx);
                std::error_code ec;
                const auto rel = std::filesystem::relative(request.sourcePath, assetRoot, ec);
                const auto relText = ec ? std::string {} : rel.generic_string();
                for (const auto& [_, entry] : assetService->registry().getRegistry())
                {
                    if (entry.type == vasset::VAssetType::eSceneManifest && entry.sourcePath == relText &&
                        !entry.importedPath.empty())
                    {
                        request.importedPath = entry.importedPath;
                        request.sourceUri = "res://" + entry.importedPath;
                        break;
                    }
                }
            }
        }

        const auto rel = sourceUriFor(ctx, request.sourcePath).empty() ? normalizedGeneric(request.sourcePath) :
                                                                         sourceUriFor(ctx, request.sourcePath);
        request.key = "model-root:" + std::string(kThumbnailCacheVersion) + ":" + rel + ":" +
                      std::to_string(fileWriteStamp(request.sourcePath));
        request.outputPath = thumbnailPathFor(request.key);
        request.status     = statusFor(request.outputPath);
        if (request.status == AssetThumbnailStatus::Missing)
            queueMissing(request);
        m_ModelRootRequestCache[cacheKey] = request;
        return request;
    }

    AssetThumbnailRequest AssetThumbnailService::requestMesh(EditorContext&   ctx,
                                                             std::string_view uuid,
                                                             std::string_view importedPath)
    {
        syncProject(ctx);

        const std::string cacheKey = std::string(uuid) + ":" + std::string(importedPath);
        if (auto cachedIt = m_MeshRequestCache.find(cacheKey); cachedIt != m_MeshRequestCache.end())
        {
            auto request = cachedIt->second;
            if (auto statusIt = m_StatusCache.find(request.key); statusIt != m_StatusCache.end())
                request.status = statusIt->second;
            return request;
        }

        AssetThumbnailRequest request;
        request.kind         = AssetThumbnailKind::Mesh;
        request.uuid         = std::string(uuid);
        request.importedPath = std::string(importedPath);
        request.sourcePath   = projectAssetRoot(ctx) / std::filesystem::path(request.importedPath);
        request.sourceUri    = request.importedPath.empty() ? std::string {} : "res://" + request.importedPath;

        uint64_t placementStamp = 0;
        if (ctx.services)
        {
            if (auto* assetService = ctx.services->tryGet<vultra::IAssetService>())
            {
                if (auto placement = findMeshSubAssetPlacement(assetService->registry(), projectAssetRoot(ctx), request.importedPath))
                    placementStamp = fileWriteStamp(placement->manifestPath);
            }
        }

        request.key = "mesh:" + std::string(kThumbnailCacheVersion) + ":" + request.uuid + ":" + request.importedPath + ":" +
                      std::to_string(fileWriteStamp(request.sourcePath)) + ":" + std::to_string(placementStamp);
        request.outputPath = thumbnailPathFor(request.key);
        request.status     = statusFor(request.outputPath);
        if (request.status == AssetThumbnailStatus::Missing)
            queueMissing(request);
        m_MeshRequestCache[cacheKey] = request;
        return request;
    }

    AssetThumbnailRequest AssetThumbnailService::requestTexture(EditorContext& ctx,
                                                                const std::filesystem::path& sourcePath)
    {
        syncProject(ctx);

        const std::string cacheKey = sourcePath.lexically_normal().generic_string();
        if (auto cachedIt = m_TextureRequestCache.find(cacheKey); cachedIt != m_TextureRequestCache.end())
        {
            auto request = cachedIt->second;
            if (auto statusIt = m_StatusCache.find(request.key); statusIt != m_StatusCache.end())
                request.status = statusIt->second;
            return request;
        }

        AssetThumbnailRequest request;
        request.kind       = AssetThumbnailKind::Texture;
        request.sourcePath = sourcePath.lexically_normal();
        request.sourceUri  = sourceUriFor(ctx, request.sourcePath);

        const auto rel = request.sourceUri.empty() ? normalizedGeneric(request.sourcePath) : request.sourceUri;
        request.key = "texture:" + std::string(kTextureThumbnailCacheVersion) + ":" + rel + ":" +
                      std::to_string(fileWriteStamp(request.sourcePath));
        request.outputPath = thumbnailPathFor(request.key);
        request.status     = statusFor(request.outputPath);
        if (request.status == AssetThumbnailStatus::Missing)
            queueMissing(request);
        m_TextureRequestCache[cacheKey] = request;
        return request;
    }

    std::filesystem::path AssetThumbnailService::projectAssetRoot(EditorContext& ctx) const
    {
        return (ctx.state.currentProject / ctx.state.currentAssetRoot).lexically_normal();
    }

    std::filesystem::path AssetThumbnailService::thumbnailPathFor(std::string_view key) const
    {
        if (m_CacheRoot.empty() || key.empty())
            return {};
        return m_CacheRoot / (fnv1a64Hex(key) + ".thumb.png");
    }

    AssetThumbnailStatus AssetThumbnailService::statusFor(const std::filesystem::path& path) const
    {
        if (path.empty())
            return AssetThumbnailStatus::Missing;

        std::error_code ec;
        return std::filesystem::exists(path, ec) && !ec ? AssetThumbnailStatus::Ready : AssetThumbnailStatus::Missing;
    }

    std::string AssetThumbnailService::sourceUriFor(EditorContext& ctx, const std::filesystem::path& sourcePath) const
    {
        const auto assetRoot = projectAssetRoot(ctx);
        std::error_code ec;
        auto rel = std::filesystem::relative(sourcePath, assetRoot, ec);
        if (ec || rel.empty())
            return {};
        return "res://" + rel.generic_string();
    }

    void AssetThumbnailService::queueMissing(AssetThumbnailRequest request)
    {
        if (request.key.empty() || request.outputPath.empty())
            return;

        const auto [it, inserted] = m_StatusCache.emplace(request.key, AssetThumbnailStatus::Queued);
        if (!inserted && it->second == AssetThumbnailStatus::Queued)
            return;

        request.status = AssetThumbnailStatus::Queued;
        m_QueuedRequests.push_back(std::move(request));
        m_TotalQueuedThisPass = std::max(m_TotalQueuedThisPass, m_QueuedRequests.size());
    }

    bool AssetThumbnailService::cookTextureThumbnail(const AssetThumbnailRequest& request)
    {
        if (request.sourcePath.empty() || request.outputPath.empty())
            return false;

        std::error_code ec;
        std::filesystem::create_directories(request.outputPath.parent_path(), ec);
        if (ec)
            return false;

        stbi_set_flip_vertically_on_load(false);
        int width = 0;
        int height = 0;
        int channels = 0;

        if (stbi_is_hdr(request.sourcePath.string().c_str()))
        {
            float* pixels = stbi_loadf(request.sourcePath.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
            if (!pixels || width <= 0 || height <= 0)
            {
                if (pixels)
                    stbi_image_free(pixels);
                return false;
            }

            std::vector<float> resized(static_cast<std::size_t>(kTextureThumbnailSize) *
                                       static_cast<std::size_t>(kTextureThumbnailSize) * 4u);
            const bool resizedOk = stbir_resize_float_linear(pixels,
                                                             width,
                                                             height,
                                                             0,
                                                             resized.data(),
                                                             kTextureThumbnailSize,
                                                             kTextureThumbnailSize,
                                                             0,
                                                             STBIR_RGBA);
            stbi_image_free(pixels);
            if (!resizedOk)
                return false;

            std::vector<unsigned char> ldr(resized.size());
            for (std::size_t i = 0; i < resized.size(); i += 4)
            {
                for (std::size_t c = 0; c < 3; ++c)
                {
                    const float mapped = resized[i + c] / (1.0f + std::max(0.0f, resized[i + c]));
                    ldr[i + c] = static_cast<unsigned char>(std::clamp(std::pow(mapped, 1.0f / 2.2f) * 255.0f,
                                                                       0.0f,
                                                                       255.0f));
                }
                ldr[i + 3] = static_cast<unsigned char>(std::clamp(resized[i + 3] * 255.0f, 0.0f, 255.0f));
            }

            return stbi_write_png(request.outputPath.string().c_str(),
                                  kTextureThumbnailSize,
                                  kTextureThumbnailSize,
                                  4,
                                  ldr.data(),
                                  kTextureThumbnailSize * 4) != 0;
        }

        unsigned char* pixels =
            stbi_load(request.sourcePath.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (!pixels || width <= 0 || height <= 0)
        {
            if (pixels)
                stbi_image_free(pixels);
            return false;
        }

        std::vector<unsigned char> resized(static_cast<std::size_t>(kTextureThumbnailSize) *
                                           static_cast<std::size_t>(kTextureThumbnailSize) * 4u);
        const bool resizedOk = stbir_resize_uint8_srgb(pixels,
                                                       width,
                                                       height,
                                                       0,
                                                       resized.data(),
                                                       kTextureThumbnailSize,
                                                       kTextureThumbnailSize,
                                                       0,
                                                       STBIR_RGBA);
        stbi_image_free(pixels);
        if (!resizedOk)
            return false;

        return stbi_write_png(request.outputPath.string().c_str(),
                              kTextureThumbnailSize,
                              kTextureThumbnailSize,
                              4,
                              resized.data(),
                              kTextureThumbnailSize * 4) != 0;
    }

    void AssetThumbnailService::prewarmProjectModelThumbnails(EditorContext& ctx)
    {
        prewarmProjectThumbnails(ctx);
    }

    void AssetThumbnailService::prewarmProjectThumbnails(EditorContext& ctx)
    {
        syncProject(ctx);
        if (!ctx.services)
            return;

        auto* assetService = ctx.services->tryGet<vultra::IAssetService>();
        if (!assetService)
            return;

        const auto assetRoot = projectAssetRoot(ctx);
        for (const auto& [uuid, entry] : assetService->registry().getRegistry())
        {
            if (entry.type == vasset::VAssetType::eSceneManifest && isModelSourcePath(entry.sourcePath))
            {
                requestModelRoot(ctx, assetRoot / std::filesystem::path(entry.sourcePath));
            }
            else if (entry.type == vasset::VAssetType::eMesh && !entry.importedPath.empty())
            {
                requestMesh(ctx, uuid, entry.importedPath);
            }
            else if (entry.type == vasset::VAssetType::eTexture && isTextureSourcePath(entry.sourcePath))
            {
                requestTexture(ctx, assetRoot / std::filesystem::path(entry.sourcePath));
            }
        }
        m_TotalQueuedThisPass = std::max(m_TotalQueuedThisPass, m_QueuedRequests.size());
    }

    bool AssetThumbnailService::processLoadingThumbnail(EditorContext& ctx, float& progress, std::string& message)
    {
        syncProject(ctx);
        ++m_FrameCounter;
        const auto totalJobs = std::max<std::size_t>(m_TotalQueuedThisPass, 1);

        if (m_ActiveRenderJob)
        {
            if (m_FrameCounter <= m_ActiveRenderJob->frameSubmitted + 1)
            {
                const auto doneJobs = totalJobs - std::min(totalJobs, m_QueuedRequests.size() + 1);
                progress = std::clamp(static_cast<float>(doneJobs) / static_cast<float>(totalJobs), 0.0f, 1.0f);
                message = "Rendering asset thumbnail " + std::to_string(doneJobs + 1) + " / " +
                          std::to_string(totalJobs) + "...";
                return true;
            }

            finishRenderJob(ctx);
        }

        if (!m_QueuedRequests.empty())
        {
            auto request = std::move(m_QueuedRequests.front());
            m_QueuedRequests.erase(m_QueuedRequests.begin());
            if (statusFor(request.outputPath) == AssetThumbnailStatus::Ready)
            {
                const auto doneJobs = totalJobs - std::min(totalJobs, m_QueuedRequests.size());
                progress = std::clamp(static_cast<float>(doneJobs) / static_cast<float>(totalJobs), 0.0f, 1.0f);
                message = "Loading cached asset thumbnail " + std::to_string(doneJobs) + " / " +
                          std::to_string(totalJobs) + "...";
                return true;
            }

            if (request.kind == AssetThumbnailKind::Texture)
            {
                const bool cooked = cookTextureThumbnail(request);
                m_StatusCache[request.key] = cooked ? AssetThumbnailStatus::Ready : AssetThumbnailStatus::Failed;
                const auto doneJobs = totalJobs - std::min(totalJobs, m_QueuedRequests.size());
                progress = std::clamp(static_cast<float>(doneJobs) / static_cast<float>(totalJobs), 0.0f, 1.0f);
                message = "Cooking texture thumbnail " + std::to_string(doneJobs) + " / " +
                          std::to_string(totalJobs) + "...";
                return true;
            }

            if (beginRenderJob(ctx, request))
            {
                const auto doneJobs = totalJobs - std::min(totalJobs, m_QueuedRequests.size() + 1);
                progress = std::clamp(static_cast<float>(doneJobs) / static_cast<float>(totalJobs), 0.0f, 1.0f);
                message = "Rendering asset thumbnail " + std::to_string(doneJobs + 1) + " / " +
                          std::to_string(totalJobs) + "...";
                return true;
            }

            m_StatusCache[request.key] = AssetThumbnailStatus::Failed;
            const auto doneJobs = totalJobs - std::min(totalJobs, m_QueuedRequests.size());
            progress = std::clamp(static_cast<float>(doneJobs) / static_cast<float>(totalJobs), 0.0f, 1.0f);
            message = "Skipping unavailable asset thumbnail " + std::to_string(doneJobs) + " / " +
                      std::to_string(totalJobs) + "...";
            return true;
        }

        progress = 1.0f;
        message = "Asset thumbnails ready.";
        m_TotalQueuedThisPass = 0;
        return false;
    }

    bool AssetThumbnailService::beginRenderJob(EditorContext& ctx, const AssetThumbnailRequest& request)
    {
        if (!ctx.services)
            return false;

        auto* backendService = ctx.services->tryGet<vultra::IRenderBackendService>();
        auto* cameraService  = ctx.services->tryGet<vultra::ICameraService>();
        auto* worldService   = ctx.services->tryGet<vultra::IWorldService>();
        auto* sceneService   = ctx.services->tryGet<vultra::ISceneService>();
        auto* assetService   = ctx.services->tryGet<vultra::IAssetService>();
        if (!backendService || !cameraService || !worldService || !sceneService || !assetService)
            return false;

        auto& world = worldService->world();
        world.clear();
        cameraService->clearManualCameras();
        addPreviewLighting(world);

        if (request.kind == AssetThumbnailKind::ModelRoot)
        {
            if (request.sourceUri.empty() ||
                sceneService->instantiateScene(world, request.sourceUri, entt::null, false) == entt::null)
            {
                world.clear();
                return false;
            }
        }
        else
        {
            vultra::CoreUUID meshUuid;
            if (!parseUuid(request.uuid, meshUuid))
                return false;
            auto meshEntity = world.createEntity();
            auto& reg = world.registry();
            reg.emplace<vultra::NameComponent>(meshEntity, vultra::NameComponent {"Thumbnail Mesh"});
            if (auto placement = findMeshSubAssetPlacement(assetService->registry(), projectAssetRoot(ctx), request.importedPath))
                reg.get<vultra::TransformComponent>(meshEntity) = placement->transform;
            reg.emplace<vultra::MeshComponent>(meshEntity, vultra::MeshComponent {.mesh = meshUuid});
        }

        const auto bounds = computeWorldMeshBounds(world, *assetService);

        auto& rd = backendService->renderDevice();
        auto format = backendService->backbuffer().getPixelFormat();
        if (format == vultra::rhi::PixelFormat::eUndefined)
            format = vultra::rhi::PixelFormat::eRGBA8_UNorm;

        ActiveRenderJob job;
        job.request = request;
        job.target = vultra::rhi::Texture::Builder {}
                         .setExtent({128u, 128u})
                         .setPixelFormat(format)
                         .setNumMipLevels(1)
                         .setUsageFlags(vultra::rhi::ImageUsage::eRenderTarget | vultra::rhi::ImageUsage::eTransferSrc |
                                        vultra::rhi::ImageUsage::eSampled)
                         .build(rd);
        if (!job.target)
        {
            world.clear();
            return false;
        }

        job.frameSubmitted = m_FrameCounter;
        m_ActiveRenderJob = std::move(job);

        auto camera = makePreviewCamera(bounds, &m_ActiveRenderJob->target);
        const glm::vec3 center = bounds.valid ? (bounds.min + bounds.max) * 0.5f : glm::vec3 {0.0f};
        const glm::vec3 cameraPosition = glm::vec3(glm::inverse(camera.view)[3]);
        setPreviewLightDirection(world, center - cameraPosition);
        cameraService->addManualCamera(camera);
        addLoadingShellCamera(*ctx.services);

        std::error_code ec;
        std::filesystem::create_directories(request.outputPath.parent_path(), ec);
        return true;
    }

    bool AssetThumbnailService::finishRenderJob(EditorContext& ctx)
    {
        if (!m_ActiveRenderJob || !ctx.services)
            return false;

        auto* backendService = ctx.services->tryGet<vultra::IRenderBackendService>();
        auto* cameraService  = ctx.services->tryGet<vultra::ICameraService>();
        auto* worldService   = ctx.services->tryGet<vultra::IWorldService>();
        if (!backendService || !cameraService || !worldService)
            return false;

        auto& job = *m_ActiveRenderJob;
        const bool saved = backendService->renderDevice().saveTextureToFile(job.target,
                                                                            job.request.outputPath.generic_string(),
                                                                            vultra::rhi::ImageAspect::eColor);
        m_StatusCache[job.request.key] = saved ? AssetThumbnailStatus::Ready : AssetThumbnailStatus::Failed;
        cameraService->clearManualCameras();
        addLoadingShellCamera(*ctx.services);
        worldService->world().clear();
        m_ActiveRenderJob.reset();
        return saved;
    }
} // namespace vultra_app::ui
