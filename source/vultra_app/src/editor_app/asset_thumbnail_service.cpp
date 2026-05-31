#include "editor_app/asset_thumbnail_service.hpp"

#include "editor_app/scene_thumbnail.hpp"

#include <vultra/function/rendering/render_structs.hpp>
#include <vultra/function/services/asset_service.hpp>
#include <vultra/function/services/camera_service.hpp>
#include <vultra/function/services/render_backend_service.hpp>
#include <vultra/function/services/render_service.hpp>
#include <vultra/function/services/scene_service.hpp>
#include <vultra/function/world/components/camera_component.hpp>
#include <vultra/function/world/components/environment_component.hpp>
#include <vultra/function/world/components/gaussian_splat_component.hpp>
#include <vultra/function/world/components/id_component.hpp>
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
#include <format>
#include <iomanip>
#include <limits>
#include <sstream>
#include <system_error>
#include <vector>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <stb_image.h>
#include <stb_image_resize2.h>
#include <stb_image_write.h>

namespace vultra_app::ui
{
    namespace
    {
        constexpr std::string_view kThumbnailCacheVersion        = "scene-override-v5";
        constexpr std::string_view kTextureThumbnailCacheVersion = "texture-v1";
        constexpr int              kTextureThumbnailSize         = 128;
        constexpr uint64_t         kRenderThumbnailWarmupFrames  = 1;
        constexpr uint64_t         kRenderThumbnailAssetWaitFrames = 180;

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
            const auto      time = std::filesystem::last_write_time(path, ec);
            if (ec)
                return 0;
            return static_cast<uint64_t>(time.time_since_epoch().count());
        }

        std::string fileContentStamp(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file)
                return std::to_string(fileWriteStamp(path));

            uint64_t hash = 14695981039346656037ull;
            char     buffer[4096];
            while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0)
            {
                const auto count = file.gcount();
                for (std::streamsize i = 0; i < count; ++i)
                {
                    hash ^= static_cast<unsigned char>(buffer[i]);
                    hash *= 1099511628211ull;
                }
            }

            std::ostringstream out;
            out << std::hex << std::setw(16) << std::setfill('0') << hash;
            return out.str();
        }

        bool isModelSourcePath(const std::string& path)
        {
            auto ext = std::filesystem::path(path).extension().generic_string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return ext == ".gltf" || ext == ".glb" || ext == ".obj" || ext == ".fbx" || ext == ".dae";
        }

        bool isTextureSourcePath(const std::string& path)
        {
            auto ext = std::filesystem::path(path).extension().generic_string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" || ext == ".hdr";
        }

        bool isSceneSourcePath(const std::filesystem::path& path)
        {
            auto ext = path.extension().generic_string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return ext == ".vscn";
        }

        bool isMaterialGraphSourcePath(const std::filesystem::path& path)
        {
            auto name = path.filename().generic_string();
            auto ext  = path.extension().generic_string();
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return ext == ".vmatgraph" || name.ends_with(".vmatgraph.json");
        }

        bool isInsideImportedFolder(const std::filesystem::path& assetRoot, const std::filesystem::path& path)
        {
            std::error_code ec;
            const auto      rel = std::filesystem::relative(path, assetRoot, ec);
            return !ec && !rel.empty() && *rel.begin() == "imported";
        }

        glm::mat4 localMatrix(const vultra::TransformComponent& t)
        {
            return glm::translate(glm::mat4 {1.0f}, t.position) * glm::mat4_cast(t.rotation) *
                   glm::scale(glm::mat4 {1.0f}, t.scale);
        }

        glm::mat4 worldMatrix(const vultra::World& world, const entt::entity entity)
        {
            const auto& reg       = world.registry();
            const auto* transform = reg.try_get<vultra::TransformComponent>(entity);
            if (!transform)
                return glm::mat4 {1.0f};

            auto       matrix = localMatrix(*transform);
            const auto parent = world.parent(entity);
            if (parent != entt::null && reg.valid(parent))
                matrix = worldMatrix(world, parent) * matrix;
            return matrix;
        }

        void updateWorldTransformsR(vultra::World& world, const entt::entity entity, const glm::mat4& parentWorld)
        {
            auto& reg       = world.registry();
            auto* transform = reg.try_get<vultra::TransformComponent>(entity);
            const glm::mat4 entityWorld =
                transform ? parentWorld * localMatrix(*transform) : parentWorld;
            if (transform)
            {
                transform->worldMatrix = entityWorld;
                transform->dirty       = false;
            }

            for (auto child = world.firstChild(entity); child != entt::null; child = world.nextSibling(child))
                updateWorldTransformsR(world, child, entityWorld);
        }

        void updateWorldTransforms(vultra::World& world)
        {
            for (auto root = world.firstChild(entt::null); root != entt::null; root = world.nextSibling(root))
                updateWorldTransformsR(world, root, glm::mat4 {1.0f});
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

        void includeGaussianSplatBounds(Bounds& bounds, const vasset::VGaussianSplat& splat, const glm::mat4& matrix)
        {
            for (const auto& point : splat.splats)
                bounds.includeTransformed(point.position, matrix);
        }

        Bounds computeWorldContentBounds(vultra::World& world, vultra::IAssetService& assets)
        {
            Bounds bounds;
            auto&  reg  = world.registry();
            auto   meshView = reg.view<vultra::TransformComponent, vultra::MeshComponent>();
            for (auto entity : meshView)
            {
                const auto& meshComponent = meshView.get<vultra::MeshComponent>(entity);
                if (!meshComponent.mesh.valid())
                    continue;

                auto handle = assets.loadMeshAsync(meshComponent.mesh);
                if (!handle.cpu())
                    continue;

                includeMeshBounds(bounds, *handle.cpu(), worldMatrix(world, entity));
            }

            auto splatView = reg.view<vultra::TransformComponent, vultra::GaussianSplatComponent>();
            for (auto entity : splatView)
            {
                const auto& splatComponent = splatView.get<vultra::GaussianSplatComponent>(entity);
                if (!splatComponent.gaussianSplat.valid())
                    continue;

                auto handle = assets.loadGaussianSplatAsync(splatComponent.gaussianSplat);
                if (!handle.cpu())
                    continue;

                includeGaussianSplatBounds(bounds, *handle.cpu(), worldMatrix(world, entity));
            }
            return bounds;
        }

        bool worldPreviewAssetsReady(vultra::World& world, vultra::IAssetService& assets)
        {
            bool hasPreviewAsset = false;
            auto& reg = world.registry();

            auto meshView = reg.view<vultra::MeshComponent>();
            for (auto entity : meshView)
            {
                (void)entity;
                const auto& meshComponent = meshView.get<vultra::MeshComponent>(entity);
                if (meshComponent.builtinGeometry != UINT32_MAX)
                    continue;
                if (!meshComponent.mesh.valid())
                    return false;
                hasPreviewAsset = true;
                if (!assets.meshPreviewReady(meshComponent.mesh))
                    return false;
            }

            auto splatView = reg.view<vultra::GaussianSplatComponent>();
            for (auto entity : splatView)
            {
                const auto& splatComponent = splatView.get<vultra::GaussianSplatComponent>(entity);
                if (!splatComponent.gaussianSplat.valid())
                    return false;
                hasPreviewAsset = true;
                auto handle = assets.loadGaussianSplatAsync(splatComponent.gaussianSplat);
                if (!handle.ready())
                    return false;
            }

            return !hasPreviewAsset || !assets.materialRefreshPending();
        }

        bool makeNearBlackTransparent(const std::filesystem::path& path)
        {
            int width = 0;
            int height = 0;
            int channels = 0;
            stbi_uc* pixels = stbi_load(path.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
            if (!pixels || width <= 0 || height <= 0)
            {
                if (pixels)
                    stbi_image_free(pixels);
                return false;
            }

            bool changed = false;
            const int pixelCount = width * height;
            for (int i = 0; i < pixelCount; ++i)
            {
                stbi_uc* px = pixels + i * 4;
                if (px[0] <= 2 && px[1] <= 2 && px[2] <= 2)
                {
                    px[3] = 0;
                    changed = true;
                }
            }

            bool ok = true;
            if (changed)
                ok = stbi_write_png(path.string().c_str(), width, height, 4, pixels, width * 4) != 0;
            stbi_image_free(pixels);
            return ok;
        }

        void addPreviewLighting(vultra::World& world)
        {
            auto& reg = world.registry();

            auto light = world.createEntity();
            reg.emplace<vultra::NameComponent>(light, vultra::NameComponent {"Thumbnail Light"});
            auto& lightTransform = reg.get<vultra::TransformComponent>(light);
            lightTransform.rotation =
                glm::quatLookAt(glm::normalize(glm::vec3 {0.4f, -0.55f, 0.7f}), glm::vec3 {0.0f, 1.0f, 0.0f});
            reg.emplace<vultra::LightComponent>(light,
                                                vultra::LightComponent {
                                                    .kind        = 0u,
                                                    .color       = glm::vec3 {1.0f},
                                                    .intensity   = 6.0f,
                                                    .castsShadow = false,
                                                });

            auto env = world.createEntity();
            reg.emplace<vultra::NameComponent>(env, vultra::NameComponent {"Thumbnail Environment"});
            reg.emplace<vultra::EnvironmentComponent>(env,
                                                      vultra::EnvironmentComponent {
                                                          .ambientColor     = glm::vec3 {0.28f, 0.30f, 0.34f},
                                                          .ambientIntensity = 1.8f,
                                                          .enableIBL        = false,
                                                      });
        }

        void setPreviewLightDirection(vultra::World& world, glm::vec3 direction)
        {
            auto&        reg         = world.registry();
            auto         view        = reg.view<vultra::NameComponent, vultra::TransformComponent>();
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
            direction        = len2 > 1e-8f ? direction * glm::inversesqrt(len2) : glm::vec3 {0.0f, -1.0f, 0.0f};
            glm::vec3 up {0.0f, 1.0f, 0.0f};
            if (std::abs(glm::dot(up, direction)) > 0.95f)
                up = glm::vec3 {1.0f, 0.0f, 0.0f};

            auto& transform    = view.get<vultra::TransformComponent>(lightEntity);
            transform.rotation = glm::normalize(glm::quatLookAtRH(direction, up));
            transform.dirty    = true;
        }

        vultra::RenderCamera makePreviewCamera(const Bounds& bounds, vultra::rhi::Texture* target)
        {
            const glm::vec3 center       = bounds.valid ? (bounds.min + bounds.max) * 0.5f : glm::vec3 {0.0f};
            const glm::vec3 size         = bounds.valid ? (bounds.max - bounds.min) : glm::vec3 {1.0f};
            float           maxDimension = std::max({size.x, size.y, size.z, 1.0f});

            const float fovY     = glm::radians(45.0f);
            float       distance = (maxDimension * 0.65f) / std::tan(fovY * 0.5f);
            distance             = std::max(distance, maxDimension * 1.6f);

            glm::vec3   offset = glm::normalize(glm::vec3 {0.5f, 0.32f, 0.62f}) * distance;
            const float radius = std::max(0.5f, maxDimension * 0.5f);

            vultra::RenderCamera camera {};
            camera.name       = "Thumbnail Camera";
            camera.view       = glm::lookAt(center + offset, center, glm::vec3 {0.0f, 1.0f, 0.0f});
            camera.projection = glm::perspectiveRH_ZO(
                fovY, 1.0f, std::max(0.01f, distance - radius * 3.0f), std::max(distance + radius * 6.0f, 1000.0f));
            camera.target      = target;
            camera.fovY        = fovY;
            camera.zNear       = std::max(0.01f, distance - radius * 3.0f);
            camera.zFar        = std::max(distance + radius * 6.0f, 1000.0f);
            camera.clearValue  = glm::vec4 {0.0f, 0.0f, 0.0f, 0.0f};
            camera.clearMode      = 0u;
            camera.suppressSkybox = true;
            camera.renderImGui    = false;
            camera.rendererKey = "universal";
            return camera;
        }

        entt::entity findPrimaryCamera(vultra::World& world)
        {
            auto& reg  = world.registry();
            auto  view = reg.view<vultra::IDComponent, vultra::TransformComponent, vultra::CameraComponent>();

            entt::entity best         = entt::null;
            int          bestPriority = std::numeric_limits<int>::min();
            for (auto e : view)
            {
                const auto& camera = view.get<vultra::CameraComponent>(e);
                if (!camera.primary)
                    continue;
                if (best == entt::null || camera.priority >= bestPriority)
                {
                    best         = e;
                    bestPriority = camera.priority;
                }
            }
            if (best != entt::null)
                return best;

            for (auto e : view)
            {
                const auto& camera = view.get<vultra::CameraComponent>(e);
                if (best == entt::null || camera.priority >= bestPriority)
                {
                    best         = e;
                    bestPriority = camera.priority;
                }
            }
            return best;
        }

        glm::mat4 makeSceneProjection(const vultra::CameraComponent& camera, const float aspect)
        {
            const float zNear = std::max(camera.zNear, 0.0001f);
            const float zFar  = std::max(camera.zFar, zNear + 0.0001f);
            if (camera.projection == 1u)
            {
                const float height = std::max(camera.orthographicHeight, 0.0001f);
                const float width  = height * std::max(aspect, 0.0001f);
                return glm::orthoRH_ZO(-width * 0.5f, width * 0.5f, -height * 0.5f, height * 0.5f, zNear, zFar);
            }

            return glm::perspectiveRH_ZO(glm::radians(camera.fovYDegrees), std::max(aspect, 0.0001f), zNear, zFar);
        }

        vultra::RenderCamera makeSceneThumbnailCamera(vultra::World&        world,
                                                      const entt::entity    entity,
                                                      vultra::rhi::Texture* target)
        {
            auto& reg    = world.registry();
            auto& camera = reg.get<vultra::CameraComponent>(entity);

            vultra::RenderCamera out {};
            if (auto* id = reg.try_get<vultra::IDComponent>(entity))
                out.uuid = id->uuid;
            out.name                    = "Thumbnail Camera";
            out.priority                = camera.priority;
            out.view                    = glm::inverse(worldMatrix(world, entity));
            out.projection              = makeSceneProjection(camera, 1.0f);
            out.zNear                   = std::max(camera.zNear, 0.0001f);
            out.zFar                    = std::max(camera.zFar, out.zNear + 0.0001f);
            out.fovY                    = glm::radians(camera.fovYDegrees);
            out.target                  = target;
            out.clearValue              = camera.clearColor;
            out.clearValue.a            = 1.0f;
            out.clearMode               = camera.clearMode;
            out.suppressSkybox          = false;
            out.renderImGui             = false;
            out.debugEntityIdOutput     = false;
            out.selectionOutlineEnabled = false;
            out.rendererKey             = camera.rendererKey.empty() ? "universal" : camera.rendererKey;
            return out;
        }

        uint64_t assetWaitFramesFor(const AssetThumbnailRequest& request)
        {
            return request.kind == AssetThumbnailKind::Scene ? 600u : kRenderThumbnailAssetWaitFrames;
        }

        bool shouldMakeThumbnailTransparent(const AssetThumbnailKind kind)
        {
            return kind == AssetThumbnailKind::ModelRoot || kind == AssetThumbnailKind::Mesh ||
                   kind == AssetThumbnailKind::MaterialGraph;
        }

        bool sourceIsNewerThanOutput(const std::filesystem::path& source, const std::filesystem::path& output)
        {
            std::error_code ec;
            const auto      sourceTime = std::filesystem::last_write_time(source, ec);
            if (ec)
                return false;
            const auto outputTime = std::filesystem::last_write_time(output, ec);
            if (ec)
                return true;
            return sourceTime > outputTime;
        }

        bool parseUuid(std::string_view text, vultra::CoreUUID& out)
        {
            vbase::UUID parsed {};
            if (!vbase::try_parse_uuid(std::string(text).c_str(), parsed))
                return false;
            out = vultra::CoreUUID(parsed);
            return true;
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

        m_ProjectRoot       = projectRoot;
        m_AssetRootName     = ctx.state.currentAssetRoot;
        m_ProjectGeneration = ctx.state.projectGeneration;
        m_CacheRoot         = m_ProjectRoot.empty() ? std::filesystem::path {} : m_ProjectRoot / ".vultra" / "thumbs";
        m_StatusCache.clear();
        m_ModelRootRequestCache.clear();
        m_MeshRequestCache.clear();
        m_TextureRequestCache.clear();
        m_SceneRequestCache.clear();
        m_MaterialGraphRequestCache.clear();
        m_QueuedRequests.clear();
        if (m_ActiveRenderJob)
        {
            if (auto* cameras = ctx.services ? ctx.services->tryGet<vultra::ICameraService>() : nullptr)
                cameras->removeManualCamerasByName("Thumbnail Camera");
            if (auto* render = ctx.services ? ctx.services->tryGet<vultra::IRenderService>() : nullptr)
                if (m_ActiveRenderJob->world)
                    render->releaseOverrideRenderWorld(m_ActiveRenderJob->world.get());
            m_ActiveRenderJob.reset();
        }
        if (m_ActiveTextureJob)
        {
            if (auto* jobs = ctx.services ? ctx.services->tryGet<vultra::IJobService>() : nullptr)
                jobs->wait(m_ActiveTextureJob->job);
        }
        m_ActiveTextureJob.reset();
        m_TotalQueuedThisPass = 0;
    }

    void AssetThumbnailService::clear(EditorContext* ctx)
    {
        if (m_ActiveRenderJob && ctx && ctx->services)
        {
            if (auto* cameras = ctx->services->tryGet<vultra::ICameraService>())
                cameras->removeManualCamerasByName("Thumbnail Camera");
            if (auto* render = ctx->services->tryGet<vultra::IRenderService>())
                if (m_ActiveRenderJob->world)
                    render->releaseOverrideRenderWorld(m_ActiveRenderJob->world.get());
        }
        m_ActiveTextureJob.reset();
        m_ProjectRoot.clear();
        m_AssetRootName.clear();
        m_CacheRoot.clear();
        m_ProjectGeneration = 0;
        m_StatusCache.clear();
        m_ModelRootRequestCache.clear();
        m_MeshRequestCache.clear();
        m_TextureRequestCache.clear();
        m_SceneRequestCache.clear();
        m_MaterialGraphRequestCache.clear();
        m_QueuedRequests.clear();
        m_ActiveRenderJob.reset();
        m_TotalQueuedThisPass = 0;
        m_FrameCounter        = 0;
    }

    AssetThumbnailRequest AssetThumbnailService::requestModelRoot(EditorContext&               ctx,
                                                                  const std::filesystem::path& sourcePath)
    {
        syncProject(ctx);

        const std::string cacheKey = sourcePath.lexically_normal().generic_string();

        AssetThumbnailRequest request;
        request.kind       = AssetThumbnailKind::ModelRoot;
        request.sourcePath = sourcePath.lexically_normal();
        request.sourceUri  = sourceUriFor(ctx, request.sourcePath);

        if (ctx.services)
        {
            if (auto* assetService = ctx.services->tryGet<vultra::IAssetService>())
            {
                const auto      assetRoot = projectAssetRoot(ctx);
                std::error_code ec;
                const auto      rel     = std::filesystem::relative(request.sourcePath, assetRoot, ec);
                const auto      relText = ec ? std::string {} : rel.generic_string();
                for (const auto& [_, entry] : assetService->registry().getRegistry())
                {
                    if (entry.type == vasset::VAssetType::eSceneManifest && entry.sourcePath == relText &&
                        !entry.importedPath.empty())
                    {
                        request.importedPath = entry.importedPath;
                        request.sourceUri    = "res://" + entry.importedPath;
                        break;
                    }
                }
            }
        }

        const auto rel = sourceUriFor(ctx, request.sourcePath).empty() ? normalizedGeneric(request.sourcePath) :
                                                                         sourceUriFor(ctx, request.sourcePath);
        request.key    = "model-root:" + std::string(kThumbnailCacheVersion) + ":" + rel + ":" +
                      std::to_string(fileWriteStamp(request.sourcePath));
        if (auto cachedIt = m_ModelRootRequestCache.find(cacheKey);
            cachedIt != m_ModelRootRequestCache.end() && cachedIt->second.key == request.key &&
            cachedIt->second.importedPath == request.importedPath)
        {
            request = cachedIt->second;
            if (auto statusIt = m_StatusCache.find(request.key); statusIt != m_StatusCache.end())
                request.status = statusIt->second;
            return request;
        }
        request.outputPath = thumbnailPathFor(request.key);
        request.status     = statusFor(request.outputPath);
        if (request.status == AssetThumbnailStatus::Missing)
            queueMissing(request);
        m_ModelRootRequestCache[cacheKey] = request;
        return request;
    }

    AssetThumbnailRequest
    AssetThumbnailService::requestMesh(EditorContext& ctx, std::string_view uuid, std::string_view importedPath)
    {
        syncProject(ctx);

        const std::string cacheKey = std::string(uuid) + ":" + std::string(importedPath);

        AssetThumbnailRequest request;
        request.kind         = AssetThumbnailKind::Mesh;
        request.uuid         = std::string(uuid);
        request.importedPath = std::string(importedPath);
        request.sourcePath   = projectAssetRoot(ctx) / std::filesystem::path(request.importedPath);
        request.sourceUri    = request.importedPath.empty() ? std::string {} : "res://" + request.importedPath;

        request.key = "mesh:" + std::string(kThumbnailCacheVersion) + ":" + request.uuid + ":" + request.importedPath +
                      ":" + std::to_string(fileWriteStamp(request.sourcePath));
        if (auto cachedIt = m_MeshRequestCache.find(cacheKey);
            cachedIt != m_MeshRequestCache.end() && cachedIt->second.key == request.key)
        {
            request = cachedIt->second;
            if (auto statusIt = m_StatusCache.find(request.key); statusIt != m_StatusCache.end())
                request.status = statusIt->second;
            return request;
        }
        request.outputPath = thumbnailPathFor(request.key);
        request.status     = statusFor(request.outputPath);
        if (request.status == AssetThumbnailStatus::Missing)
            queueMissing(request);
        m_MeshRequestCache[cacheKey] = request;
        return request;
    }

    AssetThumbnailRequest AssetThumbnailService::requestTexture(EditorContext&               ctx,
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
        request.key    = "texture:" + std::string(kTextureThumbnailCacheVersion) + ":" + rel + ":" +
                      std::to_string(fileWriteStamp(request.sourcePath));
        request.outputPath = thumbnailPathFor(request.key);
        request.status     = statusFor(request.outputPath);
        if (request.status == AssetThumbnailStatus::Missing)
            queueMissing(request);
        m_TextureRequestCache[cacheKey] = request;
        return request;
    }

    AssetThumbnailRequest AssetThumbnailService::requestScene(EditorContext&               ctx,
                                                              const std::filesystem::path& sourcePath,
                                                              const bool                   force)
    {
        syncProject(ctx);

        const std::string cacheKey = sourcePath.lexically_normal().generic_string();
        if (!force)
        {
            if (auto cachedIt = m_SceneRequestCache.find(cacheKey); cachedIt != m_SceneRequestCache.end())
            {
                auto request = cachedIt->second;
                if (auto statusIt = m_StatusCache.find(request.key); statusIt != m_StatusCache.end())
                    request.status = statusIt->second;
                return request;
            }
        }

        AssetThumbnailRequest request;
        request.kind       = AssetThumbnailKind::Scene;
        request.sourcePath = sourcePath.lexically_normal();
        request.sourceUri  = sourceUriFor(ctx, request.sourcePath);
        request.key        = "scene:" + request.sourceUri;
        request.outputPath = sceneThumbnailPath(ctx, request.sourceUri);
        request.forceRender = force;
        request.status     = statusFor(request.outputPath);
        if (force)
            request.status = AssetThumbnailStatus::Missing;
        else if (request.status == AssetThumbnailStatus::Ready &&
            sourceIsNewerThanOutput(request.sourcePath, request.outputPath))
        {
            request.status = AssetThumbnailStatus::Missing;
        }
        if (request.status == AssetThumbnailStatus::Missing)
        {
            m_StatusCache.erase(request.key);
            queueMissing(request);
        }
        m_SceneRequestCache[cacheKey] = request;
        return request;
    }

    AssetThumbnailRequest AssetThumbnailService::requestMaterialGraph(EditorContext&               ctx,
                                                                      const std::filesystem::path& sourcePath)
    {
        syncProject(ctx);

        const std::string cacheKey = sourcePath.lexically_normal().generic_string();
        AssetThumbnailRequest request;
        request.kind       = AssetThumbnailKind::MaterialGraph;
        request.sourcePath = sourcePath.lexically_normal();
        request.sourceUri  = sourceUriFor(ctx, request.sourcePath);

        const auto rel = request.sourceUri.empty() ? normalizedGeneric(request.sourcePath) : request.sourceUri;
        request.key    = "material-graph:" + std::string(kThumbnailCacheVersion) + ":" + rel + ":" +
                      fileContentStamp(request.sourcePath);
        if (auto cachedIt = m_MaterialGraphRequestCache.find(cacheKey);
            cachedIt != m_MaterialGraphRequestCache.end() && cachedIt->second.key == request.key)
        {
            request = cachedIt->second;
            if (auto statusIt = m_StatusCache.find(request.key); statusIt != m_StatusCache.end())
                request.status = statusIt->second;
            return request;
        }
        request.outputPath = thumbnailPathFor(request.key);
        request.status     = statusFor(request.outputPath);
        if (request.status == AssetThumbnailStatus::Missing)
            queueMissing(request);
        m_MaterialGraphRequestCache[cacheKey] = request;
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
        const auto      assetRoot = projectAssetRoot(ctx);
        std::error_code ec;
        auto            rel = std::filesystem::relative(sourcePath, assetRoot, ec);
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

    void AssetThumbnailService::markReady(const AssetThumbnailRequest& request)
    {
        if (!request.key.empty())
            m_StatusCache[request.key] = AssetThumbnailStatus::Ready;
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
        int width    = 0;
        int height   = 0;
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
            const bool         resizedOk = stbir_resize_float_linear(
                pixels, width, height, 0, resized.data(), kTextureThumbnailSize, kTextureThumbnailSize, 0, STBIR_RGBA);
            stbi_image_free(pixels);
            if (!resizedOk)
                return false;

            std::vector<unsigned char> ldr(resized.size());
            for (std::size_t i = 0; i < resized.size(); i += 4)
            {
                for (std::size_t c = 0; c < 3; ++c)
                {
                    const float mapped = resized[i + c] / (1.0f + std::max(0.0f, resized[i + c]));
                    ldr[i + c] =
                        static_cast<unsigned char>(std::clamp(std::pow(mapped, 1.0f / 2.2f) * 255.0f, 0.0f, 255.0f));
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
        const bool                 resizedOk = stbir_resize_uint8_srgb(
            pixels, width, height, 0, resized.data(), kTextureThumbnailSize, kTextureThumbnailSize, 0, STBIR_RGBA);
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

    bool AssetThumbnailService::startTextureThumbnailTask(EditorContext& ctx, AssetThumbnailRequest request)
    {
        if (m_ActiveTextureJob)
            return false;

        auto* jobs = ctx.services ? ctx.services->tryGet<vultra::IJobService>() : nullptr;
        if (!jobs)
        {
            const bool cooked          = cookTextureThumbnail(request);
            m_StatusCache[request.key] = cooked ? AssetThumbnailStatus::Ready : AssetThumbnailStatus::Failed;
            return true;
        }

        auto job     = std::make_unique<ActiveTextureJob>();
        job->request = std::move(request);
        auto requestCopy = job->request;
        auto done        = job->done;
        auto cooked      = job->cooked;
        job->job         = jobs->submit("Texture thumbnail",
                                vultra::JobOptions {.maxAttempts = 3},
                                [requestCopy = std::move(requestCopy), done, cooked](vultra::JobProgress& progress) {
            bool ok = false;
            for (uint32_t attempt = 1; attempt <= 3 && !ok; ++attempt)
            {
                progress.setProgress(0.1f, std::format("Decoding texture thumbnail ({}/3)...", attempt));
                ok = AssetThumbnailService::cookTextureThumbnail(requestCopy);
            }
            cooked->store(ok, std::memory_order_release);
            progress.setProgress(1.0f, ok ? "Texture thumbnail ready." : "Texture thumbnail failed.");
            done->store(true, std::memory_order_release);
        });
        m_ActiveTextureJob = std::move(job);
        return true;
    }

    bool AssetThumbnailService::collectTextureThumbnailTask(float& progress, std::string& message)
    {
        if (!m_ActiveTextureJob)
            return false;

        if (!m_ActiveTextureJob->done->load(std::memory_order_acquire))
        {
            progress = 0.5f;
            message  = "Generating texture thumbnail...";
            return true;
        }

        const auto request = std::move(m_ActiveTextureJob->request);
        const bool cooked  = m_ActiveTextureJob->cooked->load(std::memory_order_acquire);
        m_ActiveTextureJob.reset();

        m_StatusCache[request.key] = cooked ? AssetThumbnailStatus::Ready : AssetThumbnailStatus::Failed;
        progress                   = 1.0f;
        message                    = cooked ? "Generated texture thumbnail." : "Failed to generate texture thumbnail.";
        return true;
    }

    void AssetThumbnailService::prewarmProjectModelThumbnails(EditorContext& ctx) { prewarmProjectThumbnails(ctx); }

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

        std::error_code ec;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(assetRoot, ec))
        {
            if (ec)
                break;
            if (!entry.is_regular_file(ec) || ec)
                continue;
            const auto path = entry.path();
            if (isInsideImportedFolder(assetRoot, path))
                continue;
            if (isSceneSourcePath(path))
                requestScene(ctx, path);
            else if (isMaterialGraphSourcePath(path))
                requestMaterialGraph(ctx, path);
        }
        m_TotalQueuedThisPass = std::max(m_TotalQueuedThisPass, m_QueuedRequests.size());
    }

    void AssetThumbnailService::prewarmSourceThumbnails(EditorContext&                            ctx,
                                                        const std::vector<std::filesystem::path>& sourcePaths)
    {
        syncProject(ctx);
        if (sourcePaths.empty())
            return;

        for (const auto& sourcePath : sourcePaths)
        {
            std::error_code ec;
            if (!std::filesystem::is_regular_file(sourcePath, ec) || ec)
                continue;

            const auto sourcePathText = sourcePath.generic_string();
            if (isTextureSourcePath(sourcePathText))
            {
                requestTexture(ctx, sourcePath);
            }
            else if (isSceneSourcePath(sourcePath))
            {
                requestScene(ctx, sourcePath, true);
            }
            else if (isMaterialGraphSourcePath(sourcePath))
            {
                requestMaterialGraph(ctx, sourcePath);
            }
            else if (isModelSourcePath(sourcePathText))
            {
                requestModelRoot(ctx, sourcePath);
            }
        }

        m_TotalQueuedThisPass = std::max(m_TotalQueuedThisPass, m_QueuedRequests.size());
    }

    bool AssetThumbnailService::processQueuedTextureThumbnail(EditorContext& ctx, float& progress, std::string& message)
    {
        syncProject(ctx);
        ++m_FrameCounter;

        if (collectTextureThumbnailTask(progress, message))
            return true;

        auto textureIt = std::find_if(m_QueuedRequests.begin(), m_QueuedRequests.end(), [](const auto& request) {
            return request.kind == AssetThumbnailKind::Texture;
        });
        if (textureIt == m_QueuedRequests.end())
        {
            progress = 1.0f;
            message.clear();
            return false;
        }

        auto request = std::move(*textureIt);
        m_QueuedRequests.erase(textureIt);

        if (statusFor(request.outputPath) == AssetThumbnailStatus::Ready)
        {
            m_StatusCache[request.key] = AssetThumbnailStatus::Ready;
            progress = 1.0f;
            message  = "Loaded cached texture thumbnail.";
            return true;
        }

        if (!startTextureThumbnailTask(ctx, std::move(request)))
            return false;

        progress = 0.0f;
        message  = "Generating texture thumbnail...";
        return true;
    }

    bool AssetThumbnailService::processLoadingThumbnail(EditorContext& ctx, float& progress, std::string& message)
    {
        syncProject(ctx);
        ++m_FrameCounter;
        const auto totalJobs = std::max<std::size_t>(m_TotalQueuedThisPass, 1);

        if (m_ActiveRenderJob)
        {
            const auto doneJobs = totalJobs - std::min(totalJobs, m_QueuedRequests.size() + 1);
            if (!m_ActiveRenderJob->assetsReady)
            {
                if (renderJobAssetsReady(ctx, *m_ActiveRenderJob))
                {
                    m_ActiveRenderJob->assetsReady = true;
                    m_ActiveRenderJob->readyFrame  = m_FrameCounter;
                }
                else if (m_FrameCounter > m_ActiveRenderJob->frameSubmitted + assetWaitFramesFor(m_ActiveRenderJob->request))
                {
                    auto* cameraService =
                        ctx.services ? ctx.services->tryGet<vultra::ICameraService>() : nullptr;
                    auto* renderService =
                        ctx.services ? ctx.services->tryGet<vultra::IRenderService>() : nullptr;
                    if (cameraService)
                        cameraService->removeManualCamerasByName("Thumbnail Camera");
                    if (renderService && m_ActiveRenderJob->world)
                        renderService->releaseOverrideRenderWorld(m_ActiveRenderJob->world.get());
                    m_StatusCache[m_ActiveRenderJob->request.key] = AssetThumbnailStatus::Failed;
                    m_ActiveRenderJob.reset();
                    progress = std::clamp(static_cast<float>(doneJobs + 1) / static_cast<float>(totalJobs), 0.0f, 1.0f);
                    message  = "Skipping unavailable asset thumbnail " + std::to_string(doneJobs + 1) + " / " +
                              std::to_string(totalJobs) + "...";
                    return true;
                }
                else
                {
                    progress = std::clamp(static_cast<float>(doneJobs) / static_cast<float>(totalJobs), 0.0f, 1.0f);
                    message  = "Preparing asset thumbnail " + std::to_string(doneJobs + 1) + " / " +
                              std::to_string(totalJobs) + "...";
                    return true;
                }
            }

            if (m_FrameCounter <= m_ActiveRenderJob->readyFrame + kRenderThumbnailWarmupFrames)
            {
                progress = std::clamp(static_cast<float>(doneJobs) / static_cast<float>(totalJobs), 0.0f, 1.0f);
                message  = "Rendering asset thumbnail " + std::to_string(doneJobs + 1) + " / " +
                          std::to_string(totalJobs) + "...";
                return true;
            }

            finishRenderJob(ctx);
        }

        if (!m_QueuedRequests.empty())
        {
            auto renderIt = std::find_if(m_QueuedRequests.begin(), m_QueuedRequests.end(), [](const auto& request) {
                return request.kind != AssetThumbnailKind::Texture;
            });
            if (renderIt == m_QueuedRequests.end())
            {
                progress = 1.0f;
                message.clear();
                return false;
            }

            auto request = std::move(*renderIt);
            m_QueuedRequests.erase(renderIt);
            if (!request.forceRender && statusFor(request.outputPath) == AssetThumbnailStatus::Ready)
            {
                const auto doneJobs = totalJobs - std::min(totalJobs, m_QueuedRequests.size());
                progress = std::clamp(static_cast<float>(doneJobs) / static_cast<float>(totalJobs), 0.0f, 1.0f);
                message  = "Loading cached asset thumbnail " + std::to_string(doneJobs) + " / " +
                          std::to_string(totalJobs) + "...";
                return true;
            }

            if (beginRenderJob(ctx, request))
            {
                const auto doneJobs = totalJobs - std::min(totalJobs, m_QueuedRequests.size() + 1);
                progress = std::clamp(static_cast<float>(doneJobs) / static_cast<float>(totalJobs), 0.0f, 1.0f);
                message  = "Rendering asset thumbnail " + std::to_string(doneJobs + 1) + " / " +
                          std::to_string(totalJobs) + "...";
                return true;
            }

            m_StatusCache[request.key] = AssetThumbnailStatus::Failed;
            const auto doneJobs        = totalJobs - std::min(totalJobs, m_QueuedRequests.size());
            progress = std::clamp(static_cast<float>(doneJobs) / static_cast<float>(totalJobs), 0.0f, 1.0f);
            message  = "Skipping unavailable asset thumbnail " + std::to_string(doneJobs) + " / " +
                      std::to_string(totalJobs) + "...";
            return true;
        }

        progress              = 1.0f;
        message               = "Asset thumbnails ready.";
        m_TotalQueuedThisPass = 0;
        return false;
    }

    bool AssetThumbnailService::renderJobAssetsReady(EditorContext& ctx, ActiveRenderJob& job)
    {
        if (!ctx.services)
            return true;

        auto* assetService  = ctx.services->tryGet<vultra::IAssetService>();
        auto* cameraService = ctx.services->tryGet<vultra::ICameraService>();
        if (!assetService)
            return true;

        if (!job.world)
            return true;

        auto& world = *job.world;
        updateWorldTransforms(world);
        const auto sceneCamera = job.request.kind == AssetThumbnailKind::Scene ? findPrimaryCamera(world) : entt::null;
        if (job.request.kind == AssetThumbnailKind::MaterialGraph)
        {
            return !assetService->materialRefreshPending();
        }

        if (!worldPreviewAssetsReady(world, *assetService))
            return false;

        const auto bounds = computeWorldContentBounds(world, *assetService);
        if (!bounds.valid && job.request.kind != AssetThumbnailKind::Scene)
            return false;

        if (cameraService)
        {
            cameraService->removeManualCamerasByName("Thumbnail Camera");
            auto camera = makePreviewCamera(bounds, &job.target);
            if (job.request.kind == AssetThumbnailKind::Scene)
            {
                camera = sceneCamera != entt::null ? makeSceneThumbnailCamera(world, sceneCamera, &job.target) :
                                                     makePreviewCamera(bounds, &job.target);
            }
            camera.worldOverride = &world;
            const glm::vec3 center         = bounds.valid ? (bounds.min + bounds.max) * 0.5f : glm::vec3 {0.0f};
            const glm::vec3 cameraPosition = glm::vec3(glm::inverse(camera.view)[3]);
            if (job.request.kind != AssetThumbnailKind::Scene)
                setPreviewLightDirection(world, center - cameraPosition);
            cameraService->addManualCamera(camera);
        }

        return true;
    }

    bool AssetThumbnailService::beginRenderJob(EditorContext& ctx, const AssetThumbnailRequest& request)
    {
        if (!ctx.services)
            return false;

        auto* backendService = ctx.services->tryGet<vultra::IRenderBackendService>();
        auto* cameraService  = ctx.services->tryGet<vultra::ICameraService>();
        auto* sceneService   = ctx.services->tryGet<vultra::ISceneService>();
        auto* assetService   = ctx.services->tryGet<vultra::IAssetService>();
        if (!backendService || !cameraService || !sceneService || !assetService)
            return false;

        auto thumbnailWorld = std::make_unique<vultra::World>();
        auto& world         = *thumbnailWorld;
        cameraService->removeManualCamerasByName("Thumbnail Camera");

        if (request.kind == AssetThumbnailKind::ModelRoot)
        {
            addPreviewLighting(world);
            if (request.sourceUri.empty() ||
                sceneService->instantiateScene(world, request.sourceUri, entt::null, false) == entt::null)
            {
                world.clear();
                return false;
            }
        }
        else if (request.kind == AssetThumbnailKind::Scene)
        {
            if (request.sourceUri.empty())
            {
                world.clear();
                return false;
            }

            auto doc = sceneService->loadSceneSync(request.sourceUri);
            if (!doc || !doc->root)
            {
                world.clear();
                return false;
            }

            const bool emptySyntheticScene = doc->syntheticRoot && doc->root->children.empty();
            if (!emptySyntheticScene &&
                sceneService->instantiateSceneDocument(world, *doc, entt::null, false) == entt::null)
            {
                world.clear();
                return false;
            }
        }
        else if (request.kind == AssetThumbnailKind::MaterialGraph)
        {
            if (request.sourceUri.empty())
            {
                world.clear();
                return false;
            }

            addPreviewLighting(world);
            auto& reg    = world.registry();
            auto  sphere = world.createEntity();
            reg.emplace<vultra::NameComponent>(sphere, vultra::NameComponent {"Material Preview Sphere"});
            auto& transform = reg.get<vultra::TransformComponent>(sphere);
            transform.position = glm::vec3 {0.0f};
            transform.rotation = glm::quat {1.0f, 0.0f, 0.0f, 0.0f};
            transform.scale    = glm::vec3 {1.0f};
            transform.dirty    = true;
            reg.emplace<vultra::MeshComponent>(
                sphere,
                vultra::MeshComponent {
                    .builtinGeometry   = 2u,
                    .materialOverrides = {{.slot = 0u, .materialGraph = request.sourceUri}},
                });
        }
        else
        {
            addPreviewLighting(world);
            vultra::CoreUUID meshUuid;
            if (!parseUuid(request.uuid, meshUuid))
                return false;
            const auto registryEntry = assetService->registry().lookup(meshUuid.native());
            if (registryEntry.type != vasset::VAssetType::eMesh)
                return false;
            if (!request.importedPath.empty())
            {
                std::error_code ec;
                const auto      importedPath = projectAssetRoot(ctx) / std::filesystem::path(request.importedPath);
                if (!std::filesystem::is_regular_file(importedPath, ec) || ec)
                    return false;
            }
            auto  meshEntity = world.createEntity();
            auto& reg        = world.registry();
            reg.emplace<vultra::NameComponent>(meshEntity, vultra::NameComponent {"Thumbnail Mesh"});
            auto meshHandle = assetService->loadMeshAsync(meshUuid);
            if (meshHandle.cpu() && meshHandle.cpu()->hasDefaultTransform)
            {
                auto& transform    = reg.get<vultra::TransformComponent>(meshEntity);
                transform.position = meshHandle.cpu()->defaultPosition;
                transform.rotation = meshHandle.cpu()->defaultRotation;
                transform.scale    = meshHandle.cpu()->defaultScale;
                transform.dirty    = true;
            }
            reg.emplace<vultra::MeshComponent>(meshEntity, vultra::MeshComponent {.mesh = meshUuid});
        }

        updateWorldTransforms(world);

        const auto sceneCamera = request.kind == AssetThumbnailKind::Scene ? findPrimaryCamera(world) : entt::null;
        if (request.kind == AssetThumbnailKind::Scene && sceneCamera == entt::null)
            addPreviewLighting(world);

        const auto bounds = computeWorldContentBounds(world, *assetService);

        auto& rd     = backendService->renderDevice();
        auto  format = backendService->backbuffer().getPixelFormat();
        if (format == vultra::rhi::PixelFormat::eUndefined)
            format = vultra::rhi::PixelFormat::eRGBA8_UNorm;

        ActiveRenderJob job;
        job.request = request;
        job.target  = vultra::rhi::Texture::Builder {}
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
        job.world = std::move(thumbnailWorld);

        job.frameSubmitted = m_FrameCounter;
        m_ActiveRenderJob  = std::move(job);
        auto& jobWorld     = *m_ActiveRenderJob->world;

        auto camera = makePreviewCamera(bounds, &m_ActiveRenderJob->target);
        if (request.kind == AssetThumbnailKind::Scene)
        {
            camera = sceneCamera != entt::null ? makeSceneThumbnailCamera(jobWorld, sceneCamera, &m_ActiveRenderJob->target) :
                                                 makePreviewCamera(bounds, &m_ActiveRenderJob->target);
        }
        camera.worldOverride = &jobWorld;
        const glm::vec3 center         = bounds.valid ? (bounds.min + bounds.max) * 0.5f : glm::vec3 {0.0f};
        const glm::vec3 cameraPosition = glm::vec3(glm::inverse(camera.view)[3]);
        if (request.kind != AssetThumbnailKind::Scene)
            setPreviewLightDirection(jobWorld, center - cameraPosition);
        cameraService->addManualCamera(camera);

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
        auto* renderService  = ctx.services->tryGet<vultra::IRenderService>();
        if (!backendService || !cameraService)
            return false;

        auto& job = *m_ActiveRenderJob;
        cameraService->removeManualCamerasByName("Thumbnail Camera");
        const bool saved = backendService->renderDevice().saveTextureToFile(
            job.target, job.request.outputPath.generic_string(), vultra::rhi::ImageAspect::eColor);
        const bool postProcessed = saved && shouldMakeThumbnailTransparent(job.request.kind) ?
                                       makeNearBlackTransparent(job.request.outputPath) :
                                       saved;
        m_StatusCache[job.request.key] = postProcessed ? AssetThumbnailStatus::Ready : AssetThumbnailStatus::Failed;
        if (renderService && job.world)
            renderService->releaseOverrideRenderWorld(job.world.get());
        m_ActiveRenderJob.reset();
        return postProcessed;
    }
} // namespace vultra_app::ui
