#include "vultra/function/asset/asset_system.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/builtin/builtin_resources.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/structs/render_mesh.hpp"
#include "vultra/core/rhi/structs/vertex_attributes.hpp"
#include "vultra/function/asset/builtin_assets.hpp"
#include "vultra/function/asset/asset_memory_estimate.hpp"
#include "vultra/function/asset/builtin_resource_ids.hpp"
#include "vultra/function/asset/mesh_vertex_packing.hpp"
#include "vultra/function/material/material_asset.hpp"
#include "vultra/function/material/material_params.hpp"
#include "vultra/function/resource/vtexture_loader.hpp"
#include "vultra/function/rendering/srp/builtin/builtin_rendergraph_registry.hpp"
#include "vultra/function/services/render_backend_service.hpp"

#ifdef VULTRA_HAS_VASSET_IMPORT
#include "vultra/core/builtin/builtin_resources.hpp"

#include <vasset/editor_filesystem.hpp>
#include <vasset/vasset_importers.hpp>
#endif
#include <vasset/vgaussiansplat.hpp>
#include <vasset/vanimation.hpp>
#include <vasset/vmaterial.hpp>

#include <vfilesystem/backends/physical_filesystem.hpp>

#include <vtask/scheduler.hpp>
#include <vtask/task_set.hpp>

#include <glm/gtc/packing.hpp>
#include <glm/gtx/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <string_view>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#if !defined(_WIN32) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
extern "C"
{
    extern const std::byte vultra_builtin_citrus_orchard_sky_texture_start[];
    extern const std::byte vultra_builtin_citrus_orchard_sky_texture_end[];
}
#endif

namespace vultra
{
    namespace
    {
        constexpr uint32_t kAssetLoadMaxAttempts = 3;

        template<class Record>
        void markAssetUsed(Record& record, const uint64_t frameIndex)
        {
            record.lastUsedFrame.store(frameIndex, std::memory_order_release);
        }

        template<class Cache>
        void releaseZeroRefCpuAssetCache(Cache& cache, const uint64_t frameIndex, const uint64_t idleFrames)
        {
            cache.forEachRecord([&](auto& record) {
                if (!record.cpu)
                    return;
                if (record.state.load(std::memory_order_acquire) != AssetState::eReady)
                    return;
                if (record.refCount.load(std::memory_order_acquire) != 0)
                    return;

                const uint64_t lastUsed = record.lastUsedFrame.load(std::memory_order_acquire);
                if (frameIndex < lastUsed || frameIndex - lastUsed < idleFrames)
                    return;

                record.cpu.reset();
                record.gpuIndex.store(std::numeric_limits<uint32_t>::max(), std::memory_order_release);
                record.state.store(AssetState::eUnloaded, std::memory_order_release);
            });
        }

        bool isBuiltinCitrusOrchardSkyTextureUri(std::string_view uri)
        {
            return uri == kBuiltinCitrusOrchardSkyTextureUri;
        }

        bool isBuiltinTextureUri(std::string_view uri)
        {
            return uri.starts_with(kBuiltinTextureUriPrefix);
        }

        bool isBuiltinMaterialUri(std::string_view uri)
        {
            return uri.starts_with(kBuiltinMaterialUriPrefix);
        }

        std::filesystem::path builtinTexturePathForUri(std::string_view uri)
        {
            if (!isBuiltinTextureUri(uri))
                return {};
            const auto rel = std::string(uri.substr(kBuiltinTextureUriPrefix.size()));
            return (std::filesystem::path("builtin") / "textures" / std::filesystem::path(rel)).lexically_normal();
        }

        std::filesystem::path builtinMaterialPathForUri(std::string_view uri)
        {
            if (!isBuiltinMaterialUri(uri))
                return {};
            const auto rel = std::string(uri.substr(kBuiltinMaterialUriPrefix.size()));
            return (std::filesystem::path("builtin") / "materials" / std::filesystem::path(rel)).lexically_normal();
        }

        vbase::Result<std::string, std::string> readBuiltinTextFile(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file)
                return vbase::Result<std::string, std::string>::err("builtin text asset not found");

            const auto  size = static_cast<std::streamsize>(file.tellg());
            std::string text(static_cast<size_t>(std::max<std::streamsize>(size, 0)), '\0');
            file.seekg(0);
            if (!text.empty() && !file.read(text.data(), size))
                return vbase::Result<std::string, std::string>::err("failed to read builtin text asset");
            return vbase::Result<std::string, std::string>::ok(std::move(text));
        }

        bool isLoadableBuiltinTexturePath(const std::filesystem::path& path)
        {
            const auto ext = path.extension().generic_string();
            return ext == ".vtexture" || ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" ||
                   ext == ".tga" || ext == ".hdr" || ext == ".pic" || ext == ".exr" || ext == ".ktx" ||
                   ext == ".ktx2" || ext == ".dds";
        }

        std::string builtinTextureUriForPath(const std::filesystem::path& path)
        {
            std::error_code ec;
            const auto rel = std::filesystem::relative(path.lexically_normal(),
                                                       (std::filesystem::path("builtin") / "textures").lexically_normal(),
                                                       ec);
            if (ec || rel.empty())
                return {};
            return std::string(kBuiltinTextureUriPrefix) + rel.generic_string();
        }

        std::string builtinTextureUriForUuid(const CoreUUID& uuid)
        {
            if (uuid == builtinCitrusOrchardSkyTextureUuid())
                return std::string(kBuiltinCitrusOrchardSkyTextureUri);

            const auto root = std::filesystem::path("builtin") / "textures";
            std::error_code ec;
            if (!std::filesystem::exists(root, ec) || ec)
                return {};
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
                if (!entry.is_regular_file(ec) || ec || !isLoadableBuiltinTexturePath(entry.path()))
                {
                    ec.clear();
                    continue;
                }
                const auto uri = builtinTextureUriForPath(entry.path());
                if (!uri.empty() && builtinTextureUuidForUri(uri) == uuid)
                    return uri;
            }
            return {};
        }

        vbase::Result<std::vector<std::byte>, std::string> readResourceBuiltinTextureBytes(std::string_view uri)
        {
            if (!isBuiltinCitrusOrchardSkyTextureUri(uri))
                return vbase::Result<std::vector<std::byte>, std::string>::err("builtin texture resource not found");

#if defined(_WIN32)
            HMODULE module = GetModuleHandleW(nullptr);
            HRSRC   res    = FindResourceW(module,
                                           MAKEINTRESOURCEW(kBuiltinResourceCitrusOrchardSkyTexture),
                                           MAKEINTRESOURCEW(10));
            if (!res)
                return vbase::Result<std::vector<std::byte>, std::string>::err("builtin texture resource not found");

            HGLOBAL loaded = LoadResource(module, res);
            const auto* data = loaded ? static_cast<const std::byte*>(LockResource(loaded)) : nullptr;
            const DWORD size = SizeofResource(module, res);
            if (!data || size == 0)
                return vbase::Result<std::vector<std::byte>, std::string>::err("builtin texture resource is empty");

            std::vector<std::byte> bytes(size);
            std::memcpy(bytes.data(), data, size);
            return vbase::Result<std::vector<std::byte>, std::string>::ok(std::move(bytes));
#elif !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
            const auto* begin = vultra_builtin_citrus_orchard_sky_texture_start;
            const auto* end   = vultra_builtin_citrus_orchard_sky_texture_end;
            if (!begin || !end || end <= begin)
                return vbase::Result<std::vector<std::byte>, std::string>::err("builtin texture resource is empty");

            std::vector<std::byte> bytes(static_cast<size_t>(end - begin));
            std::memcpy(bytes.data(), begin, bytes.size());
            return vbase::Result<std::vector<std::byte>, std::string>::ok(std::move(bytes));
#else
            return vbase::Result<std::vector<std::byte>, std::string>::err("builtin texture resource not available");
#endif
        }

        vasset::VTextureFileFormat textureFileFormatForExtension(const std::filesystem::path& path)
        {
            const auto ext = path.extension().generic_string();
            using enum vasset::VTextureFileFormat;
            if (ext == ".jpg")
                return eJPG;
            if (ext == ".jpeg")
                return eJPEG;
            if (ext == ".png")
                return ePNG;
            if (ext == ".tga")
                return eTGA;
            if (ext == ".bmp")
                return eBMP;
            if (ext == ".hdr")
                return eHDR;
            if (ext == ".pic")
                return ePIC;
            if (ext == ".exr")
                return eEXR;
            if (ext == ".ktx")
                return eKTX;
            if (ext == ".dds")
                return eDDS;
            if (ext == ".ktx2")
                return eKTX2;
            return eUnknown;
        }

        std::unique_ptr<vasset::VTexture> makeTextureFromBytes(std::string_view               uri,
                                                               const std::filesystem::path&   path,
                                                               const std::vector<std::byte>& bytes)
        {
            if (path.extension() == ".vtexture")
            {
                auto cpu = std::make_unique<vasset::VTexture>();
                auto r   = vasset::loadTextureFromMemory(bytes, *cpu);
                return r ? std::move(cpu) : nullptr;
            }

            auto fileFormat = textureFileFormatForExtension(path);
            if (fileFormat == vasset::VTextureFileFormat::eUnknown)
                return nullptr;

            auto cpu       = std::make_unique<vasset::VTexture>();
            cpu->uuid      = builtinTextureUuidForUri(uri).native();
            cpu->fileFormat = fileFormat;
            cpu->data.resize(bytes.size());
            std::memcpy(cpu->data.data(), bytes.data(), bytes.size());
            return cpu;
        }

        std::unique_ptr<vasset::VTexture> makeTextureFromBytes(std::string_view               uri,
                                                               const std::vector<std::byte>& bytes)
        {
            return makeTextureFromBytes(uri, builtinTexturePathForUri(uri), bytes);
        }

        // Fixed builtin/imported PBR parameter block lives in
        // vultra/function/material/material_params.hpp. Shader-backed materials use
        // vshadersystem reflection offsets when their assets are resolved.

        std::string sanitizeMaterialAssetSegment(std::string text)
        {
            if (text.empty())
                text = "material";
            for (char& c : text)
            {
                const auto ch = static_cast<unsigned char>(c);
                if (!std::isalnum(ch) && c != '_' && c != '-' && c != '.')
                    c = '_';
            }
            while (!text.empty() && (text.front() == '_' || text.front() == '.'))
                text.erase(text.begin());
            if (text.empty())
                text = "material";
            return text;
        }

        std::filesystem::path importedMaterialAssetRelativePath(const std::string_view meshImportedPath,
                                                                const uint32_t         slot,
                                                                const std::string_view materialName)
        {
            const auto meshKey = sanitizeMaterialAssetSegment(
                std::filesystem::path(std::string(meshImportedPath)).filename().generic_string());
            const auto materialKey = sanitizeMaterialAssetSegment(std::string(materialName));
            return std::filesystem::path("materials") / "imported" / meshKey /
                   (std::to_string(slot) + "_" + materialKey + ".vmat.json");
        }

        nlohmann::json jsonVec4(const glm::vec4& v)
        {
            return nlohmann::json::array({v.x, v.y, v.z, v.w});
        }

        nlohmann::json jsonVec3(const glm::vec3& v)
        {
            return nlohmann::json::array({v.x, v.y, v.z});
        }

        std::optional<glm::vec4> jsonVec4Value(const nlohmann::json& value)
        {
            if (!value.is_array() || value.size() != 4)
                return std::nullopt;

            glm::vec4 out {1.0f};
            for (size_t i = 0; i < 4; ++i)
            {
                if (!value[i].is_number())
                    return std::nullopt;
                out[static_cast<glm::length_t>(i)] = value[i].get<float>();
            }
            return out;
        }

        std::optional<float> jsonFloatValue(const nlohmann::json& value)
        {
            if (!value.is_number())
                return std::nullopt;
            return value.get<float>();
        }

        std::optional<bool> jsonBoolValue(const nlohmann::json& value)
        {
            if (!value.is_boolean())
                return std::nullopt;
            return value.get<bool>();
        }

        std::optional<std::string> jsonStringValue(const nlohmann::json& value)
        {
            if (!value.is_string())
                return std::nullopt;
            return value.get<std::string>();
        }

        const char* alphaModeString(const vasset::VMaterialAlphaMode mode)
        {
            switch (mode)
            {
                case vasset::VMaterialAlphaMode::eMask:
                    return "Mask";
                case vasset::VMaterialAlphaMode::eBlend:
                    return "Blend";
                case vasset::VMaterialAlphaMode::eOpaque:
                default:
                    return "Opaque";
            }
        }

        uint32_t alphaModeFromString(const std::string_view text)
        {
            if (text == "Mask" || text == "mask")
                return static_cast<uint32_t>(vasset::VMaterialAlphaMode::eMask);
            if (text == "Blend" || text == "blend")
                return static_cast<uint32_t>(vasset::VMaterialAlphaMode::eBlend);
            return static_cast<uint32_t>(vasset::VMaterialAlphaMode::eOpaque);
        }

        float roughnessFromPhongShininess(const float shininess)
        {
            if (!(shininess > 0.0f))
                return 1.0f;
            return std::clamp(std::sqrt(2.0f / (shininess + 2.0f)), 0.045f, 1.0f);
        }

        // MaterialParamsPBRSG / MaterialParamsUnlit / MaterialParamsPhong now live in
        // vultra/function/material/material_params.hpp (single byte-layout source of
        // truth shared with the render system and the graph constant path).

#ifdef VULTRA_HAS_VASSET_IMPORT
        vasset::VAssetImporter::ImportOptions
        makeAssetImportOptions(const bool importShaderLibraries = true,
                               std::vector<AssetDiagnostic>* diagnostics = nullptr)
        {
            vasset::VAssetImporter::ImportOptions options;
            options.importShaderLibraries = importShaderLibraries;
            if (diagnostics)
            {
                options.diagnostics = [diagnostics](const vasset::VAssetImporter::ImportOptions::Diagnostic& diagnostic) {
                    diagnostics->push_back(AssetDiagnostic {
                        .path    = diagnostic.path,
                        .line    = diagnostic.line,
                        .column  = diagnostic.column,
                        .message = diagnostic.message,
                    });
                };
            }
            // Builtin GLSL includes are provided to the importer as a single VFS
            // mount (see runShaderCompiler). Each include is exposed once by its
            // canonical "include/..." path; the VFS resolves it by absolute path
            // from any shader directory, so no per-path duplication is needed.
            for (auto& [virtualPath, sourceText] : builtin::shaderIncludeSources())
            {
                options.shaderVirtualIncludes.push_back({
                    .virtualPath = std::move(virtualPath),
                    .sourceText  = std::move(sourceText),
                });
            }
            return options;
        }
#endif

        constexpr std::size_t kMaxUploadCommandsPerFrame        = 2;
        constexpr std::size_t kMaxMaterialRefreshChecksPerFrame = 8;

        using namespace resource;
        using namespace rhi;
        using namespace vasset;

        // buildVertexAttributes / PackedVertexLayout / packVertices now live in
        // vultra/function/asset/mesh_vertex_packing.hpp.

        [[nodiscard]] bool materialNeedsAnyHit(const vasset::VMaterial& material)
        {
            if (material.model != vasset::VMaterialModel::ePBRMetallicRoughness)
                return false;

            return material.core.pbrMR.alphaMode == vasset::VMaterialAlphaMode::eMask;
        }

        enum class PbrMrTextureMode : uint32_t
        {
            eGltfMetallicRoughness = 0,
            eOcclusionRoughnessMetallic = 1,
        };

        [[nodiscard]] CoreUUID pbrMrCombinedTextureUuid(const vasset::VMaterialPBRMetallicRoughness& pbr)
        {
            const CoreUUID gltfMrUuid(pbr.metallicRoughnessTexture.uuid);
            if (gltfMrUuid.valid())
                return gltfMrUuid;
            return CoreUUID(pbr.specularTexture.uuid);
        }

        [[nodiscard]] PbrMrTextureMode pbrMrTextureMode(const vasset::VMaterialPBRMetallicRoughness& pbr)
        {
            if (CoreUUID(pbr.metallicRoughnessTexture.uuid).valid())
                return PbrMrTextureMode::eGltfMetallicRoughness;
            if (CoreUUID(pbr.specularTexture.uuid).valid())
                return PbrMrTextureMode::eOcclusionRoughnessMetallic;
            return PbrMrTextureMode::eGltfMetallicRoughness;
        }

        float sigmoid(float x) { return 1.0f / (1.0f + std::exp(-x)); }

        // estimateV*Bytes / stringBytes / vectorBytes now live in
        // vultra/function/asset/asset_memory_estimate.hpp.

        float clampToF16(float x)
        {
            // IEEE half max finite value.
            return std::clamp(x, -65504.0f, 65504.0f);
        }

        uint32_t packF16x2(float a, float b) { return glm::packHalf2x16(glm::vec2(clampToF16(a), clampToF16(b))); }

        uint32_t packF16x2Clamp01(float a, float b)
        {
            return glm::packHalf2x16(glm::vec2(std::clamp(a, 0.0f, 1.0f), std::clamp(b, 0.0f, 1.0f)));
        }

        // External asset/user-facing quaternion vectors stay in xyzw order.
        // GLM's quat constructor expects wxyz, so this is the only place where
        // we intentionally bridge between the two conventions for 3DGS assets.
        glm::quat sanitizeAndNormalizeQuatFromExternalXyzw(const glm::vec4& xyzw)
        {
            if (!std::isfinite(xyzw.x) || !std::isfinite(xyzw.y) || !std::isfinite(xyzw.z) || !std::isfinite(xyzw.w))
                return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

            glm::quat   q(xyzw.w, xyzw.x, xyzw.y, xyzw.z);
            const float len2 = glm::dot(q, q);
            if (!(len2 > 1e-12f))
                return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

            return glm::normalize(q);
        }

        bool shouldReadPhysicalTextSourceDirectly(const std::filesystem::path& path)
        {
            auto ext = path.extension().generic_string();
            std::ranges::transform(
                ext, ext.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

            const auto filename = path.filename().generic_string();
            return ext == ".vscn" || ext == ".vmanifest" || ext == ".lua" || ext == ".vmatgraph" ||
                   filename.ends_with(".vrg.json") || filename.ends_with(".vmatgraph.json") ||
                   filename.ends_with(".vmat.json") || filename.ends_with(".vmatnode.json") ||
                   filename.ends_with(".vshaderlib.lua") || filename.ends_with(".vso.lua") ||
                   filename.ends_with(".vsrp.lua") || filename.ends_with(".vfeature.lua");
        }
    } // namespace

    AssetSystem::~AssetSystem() = default;

    bool AssetSystem::onInit()
    {
        VULTRA_CORE_INFO("[AssetSystem] Initializing...");

        VULTRA_CORE_TRACE("[AssetSystem] Getting render backend");
        if (auto* backend = ctx().services.tryGet<IRenderBackendService>())
            m_RenderDevice = &backend->renderDevice();

        VULTRA_CORE_TRACE("[AssetSystem] Getting GPU resource service");
        m_GpuResourceService = ctx().services.tryGet<IGpuResourceService>();
        if (!m_RenderDevice || !m_GpuResourceService)
            VULTRA_CORE_INFO("[AssetSystem] GPU services unavailable; running CPU/text asset mode.");
        m_CpuLoadScheduler = std::make_unique<vtask::Scheduler>();

        // Default config (can be overridden at runtime/editor).
        configure(AssetSystemDesc {
            .assetRoot        = ctx().config.asset.assetRoot,
            .importedFolder   = ctx().config.asset.importedFolder,
            .registryFile     = ctx().config.asset.registryFile,
            .vpkFile          = ctx().config.asset.vpkFile,
            .enableImportScan = ctx().config.asset.enableImportScan,
            .asyncLoading     = ctx().config.asset.asyncLoading,
        });

        VULTRA_CORE_TRACE("[AssetSystem] Providing IAssetService");
        ctx().services.provide<IAssetService>(this);

        VULTRA_CORE_INFO("[AssetSystem] Initialized!");

        return true;
    }

    void AssetSystem::onShutdown()
    {
        VULTRA_CORE_INFO("[AssetSystem] Shutting down");
        if (auto* backend = ctx().services.tryGet<IRenderBackendService>())
            backend->renderDevice().waitIdle();

        waitForCpuLoadTasks();
        {
            std::scoped_lock lock(m_UploadQueueMutex);
            m_UploadQueue.clear();
        }
        m_MeshCache.clear();
        m_TextureCache.clear();
        m_GaussianSplatCache.clear();
        m_SkeletonCache.clear();
        m_AnimationCache.clear();
        m_TexUUIDToBindlessIndex.clear();
        m_PendingMaterialRefreshes.clear();
        m_CpuLoadScheduler.reset();
        m_RenderDevice       = nullptr;
        m_GpuResourceService = nullptr;
    }

    AssetMemoryStats AssetSystem::memoryStats() const
    {
        AssetMemoryStats stats {};

        auto addMesh = [&stats](const auto& record) {
            if (record.cpu)
            {
                stats.cpuCacheBytes += sizeof(record);
                stats.cpuCacheBytes += estimateVMeshBytes(*record.cpu);
            }
        };

        m_MeshCache.forEachRecord(addMesh);

        auto addTexture = [&stats](const auto& record) {
            if (record.cpu)
            {
                stats.cpuCacheBytes += sizeof(record);
                stats.cpuCacheBytes += estimateVTextureBytes(*record.cpu);
            }
        };

        m_TextureCache.forEachRecord(addTexture);

        auto addSplat = [&stats](const auto& record) {
            if (record.cpu)
            {
                stats.cpuCacheBytes += sizeof(record);
                stats.cpuCacheBytes += estimateVGaussianSplatBytes(*record.cpu);
            }
        };

        m_GaussianSplatCache.forEachRecord(addSplat);

        auto addSkeleton = [&stats](const auto& record) {
            if (record.cpu)
            {
                stats.cpuCacheBytes += sizeof(record);
                stats.cpuCacheBytes += estimateVSkeletonBytes(*record.cpu);
            }
        };

        m_SkeletonCache.forEachRecord(addSkeleton);

        auto addAnimation = [&stats](const auto& record) {
            if (record.cpu)
            {
                stats.cpuCacheBytes += sizeof(record);
                stats.cpuCacheBytes += estimateVAnimationBytes(*record.cpu);
            }
        };

        m_AnimationCache.forEachRecord(addAnimation);

        return stats;
    }

    void AssetSystem::configure(const AssetSystemDesc& desc)
    {
        if (auto* backend = ctx().services.tryGet<IRenderBackendService>())
            backend->renderDevice().waitIdle();

        waitForCpuLoadTasks();
        {
            std::scoped_lock lock(m_UploadQueueMutex);
            m_UploadQueue.clear();
        }
        m_MeshCache.clear();
        m_TextureCache.clear();
        m_GaussianSplatCache.clear();
        m_TexUUIDToBindlessIndex.clear();
        m_PendingMaterialRefreshes.clear();
        if (m_GpuResourceService)
            m_GpuResourceService->pool().clear();

        m_Desc = desc;
        {
            std::scoped_lock lock(m_TextOverrideMutex);
            m_TextAssetOverrides.clear();
        }
        if (m_Desc.assetRoot.empty())
            m_Desc.assetRoot = ctx().config.asset.assetRoot;
        if (m_Desc.importedFolder.empty())
            m_Desc.importedFolder = ctx().config.asset.importedFolder;
        if (m_Desc.registryFile.empty())
            m_Desc.registryFile = ctx().config.asset.registryFile;
        if (m_Desc.scheme.empty())
            m_Desc.scheme = "res";
        if (m_Desc.vpkFile.empty())
            m_Desc.vpkFile = ctx().config.asset.vpkFile;

        const auto resolveVpkPath = [&](const std::string& p) {
            std::filesystem::path path {p};
#if defined(__EMSCRIPTEN__)
            // wasm preloads VPK at "/resources.vpk", keep this deterministic.
            if (path.is_relative())
                path = std::filesystem::path("/") / path;
#endif
            return path.lexically_normal();
        };

        m_Registry.setAssetRootPath(m_Desc.assetRoot);
        m_Registry.setImportedFolderName(m_Desc.importedFolder);

        if (ctx().config.asset.loadFromVPK)
        {
            // For production, mount the VPK file (read-only). The VPK's embedded registry is the source of truth
            // for UUID -> source path mapping.
            const auto vpkPath       = resolveVpkPath(m_Desc.vpkFile).generic_string();
            auto       vpkFileSystem = createRef<vasset::VpkFileSystem>(vpkPath);
            auto       openResult    = vpkFileSystem->openPackage();
            if (!openResult)
            {
                VULTRA_CORE_ERROR("[AssetSystem] Failed to open VPK file: {}", vpkPath);
                return;
            }
            m_Desc.vpkFile = vpkPath;

            m_Registry = vasset::VAssetRegistry {};
            m_Registry.setAssetRootPath(m_Desc.assetRoot);
            m_Registry.setImportedFolderName(m_Desc.importedFolder);

            const auto& vpk = vpkFileSystem->getVpk();
            for (const auto& entry : vpk.registry)
            {
                if (entry.pathOffset + entry.pathSize > vpk.stringTable.size())
                    continue;

                const char*       s = vpk.stringTable.data() + entry.pathOffset;
                const std::string logicalPath(s, entry.pathSize);
                const std::string ext = std::filesystem::path(logicalPath).extension().generic_string();

                vasset::VAssetType inferredType = vasset::VAssetType::eUnknown;
                if (ext == ".vscn")
                    inferredType = vasset::VAssetType::eScene;
                else if (ext == ".vmanifest" || ext == ".gltf" || ext == ".glb" || ext == ".fbx" || ext == ".obj" ||
                         ext == ".dae")
                    inferredType = vasset::VAssetType::eSceneManifest;
                else if (ext == ".lua")
                    inferredType = vasset::VAssetType::eScriptLua;
                else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" ||
                         ext == ".gif" || ext == ".psd" || ext == ".pic" || ext == ".hdr" || ext == ".ktx" ||
                         ext == ".dds" || ext == ".ktx2")
                    inferredType = vasset::VAssetType::eTexture;
                else if (ext == ".ply" || ext == ".spz" || ext == ".splat" || ext == ".ksplat")
                    inferredType = vasset::VAssetType::eGaussianSplat;
                else if (logicalPath.rfind(m_Desc.importedFolder + "/mesh/", 0) == 0)
                    inferredType = vasset::VAssetType::eMesh;

                const auto registryType = entry.type == vasset::VAssetType::eUnknown ? inferredType : entry.type;
                m_Registry.registerAsset(entry.uuid, logicalPath, logicalPath, registryType);
            }

            m_Resolver.loadFromVPK(vpk);
            m_VFS.mount(vpkFileSystem, m_Desc.scheme);
        }
        else
        {
            // For development, mount the editor remap filesystem, which allows transparent access to source assets and
            // imported assets. Runtime-only builds mount the physical filesystem directly and expect pre-baked assets.
            auto registryPath = (std::filesystem::path(m_Desc.assetRoot) / m_Desc.importedFolder / m_Desc.registryFile)
                                    .generic_string();
            const bool hasRegistryFile = std::filesystem::exists(registryPath);
            const bool loadedRegistry  = hasRegistryFile && m_Registry.load(registryPath);
            if (loadedRegistry)
            {
                const auto beforeCleanup = m_Registry.getRegistry().size();
                m_Registry.cleanup();
                const auto afterCleanup = m_Registry.getRegistry().size();
                if (afterCleanup < beforeCleanup)
                {
                    VULTRA_CORE_INFO("[AssetSystem] Removed {} stale asset registry entr{} from '{}'",
                                     beforeCleanup - afterCleanup,
                                     beforeCleanup - afterCleanup == 1 ? "y" : "ies",
                                     registryPath);
                    if (!m_Registry.save(registryPath))
                    {
                        VULTRA_CORE_WARN("[AssetSystem] Failed to save reconciled asset registry: {}", registryPath);
                    }
                }
            }
            if (m_Desc.enableImportScan && (!loadedRegistry || m_Registry.getRegistry().empty()))
            {
                if (!loadedRegistry)
                {
                    VULTRA_CORE_WARN("[AssetSystem] Failed to load asset registry from file: {}", registryPath);
                }
                else
                {
                    VULTRA_CORE_WARN("[AssetSystem] Asset registry is empty, rebuilding: {}", registryPath);
                }
#ifdef VULTRA_HAS_VASSET_IMPORT
                vasset::VAssetImporter importer {m_Registry};
                importer.setOptions(makeAssetImportOptions(false));
                importer.importOrReimportAssetFolder(m_Desc.assetRoot);
                m_Registry.save(registryPath);
#else
                VULTRA_CORE_WARN("[AssetSystem] Runtime-only vasset build cannot auto-import source assets. "
                                 "Expect pre-baked assets or an existing registry.");
#endif
            }
            else
            {
                if (loadedRegistry)
                {
                    VULTRA_CORE_INFO("[AssetSystem] Loaded asset registry from file: {}", registryPath);
                }
                else
                {
                    VULTRA_CORE_WARN("[AssetSystem] Asset import scan disabled and registry is unavailable: {}",
                                     registryPath);
                }
            }

#ifdef VULTRA_HAS_VASSET_IMPORT
            if (m_Desc.enableImportScan)
            {
                vasset::VAssetImporter importer {m_Registry};
                importer.setOptions(makeAssetImportOptions(false));
                auto importResult = importer.importOrReimportAssetFolder(m_Desc.assetRoot, false);
                if (!importResult)
                {
                    VULTRA_CORE_WARN("[AssetSystem] Asset import scan failed while checking stale editor assets.");
                }
                m_Registry.save(registryPath);
            }
#endif

            m_Resolver.loadFromAssetRegistry(m_Registry);

#ifdef VULTRA_HAS_VASSET_IMPORT
            m_VFS.mount(createRef<vasset::EditorRemapFileSystem>(
                            createRef<vfilesystem::PhysicalFileSystem>(vfilesystem::Path {m_Desc.assetRoot})),
                        m_Desc.scheme);
#else
            m_VFS.mount(createRef<vfilesystem::PhysicalFileSystem>(vfilesystem::Path {m_Desc.assetRoot}),
                        m_Desc.scheme);
#endif
        }

        m_Resolver.setScheme(m_Desc.scheme);

        if (!m_RenderDevice || !m_GpuResourceService)
        {
            VULTRA_CORE_INFO("[AssetSystem] Asset registry configured. Registry entries: {}",
                             m_Registry.getRegistry().size());
            return;
        }

        auto& pool = m_GpuResourceService->pool();

        // Global bindless texture table: reserve slot 0 as fallback.
        pool.ensureBindlessSlot0(*m_RenderDevice);

        // Reset global material param pool.
        pool.materialParams.reset();

        VULTRA_CORE_INFO("[AssetSystem] Asset registry configured. Registry entries: {}",
                         m_Registry.getRegistry().size());
    }

    void AssetSystem::update(uint64_t frameIndex)
    {
        m_LastUpdateFrame = std::max(m_LastUpdateFrame, frameIndex);

        // NOTE:
        // GPU upload must happen on the main/render thread. Even in sync bring-up, we keep a queue + update() shape so
        // the system can migrate to async loading later without breaking APIs.
        collectFinishedCpuLoadTasks();
        if (!m_RenderDevice || !m_GpuResourceService)
        {
            std::scoped_lock lock(m_UploadQueueMutex);
            m_UploadQueue.clear();
            m_PendingMaterialRefreshes.clear();
            (void)frameIndex;
            return;
        }

        refreshPendingMaterialParams();

        // Drain upload commands
        std::vector<UploadCmd> cmds;
        {
            std::scoped_lock lock(m_UploadQueueMutex);
            const auto count = std::min(kMaxUploadCommandsPerFrame, m_UploadQueue.size());
            cmds.insert(cmds.end(), m_UploadQueue.begin(), m_UploadQueue.begin() + static_cast<std::ptrdiff_t>(count));
            m_UploadQueue.erase(m_UploadQueue.begin(), m_UploadQueue.begin() + static_cast<std::ptrdiff_t>(count));
        }

        for (const auto& cmd : cmds)
        {
            switch (cmd.kind)
            {
                case UploadCmd::Kind::eMesh: {
                    auto* rec = m_MeshCache.findOrCreate(cmd.uuid);
                    if (!rec)
                        break;

                    auto st = rec->state.load(std::memory_order_acquire);
                    if (st != AssetState::eUploadQueued && st != AssetState::eCPUReady)
                        break;

                    rec->state.store(AssetState::eUploadingGPU, std::memory_order_release);

                    // TODO (deferred upload):
                    // - Create GPU materials (may trigger texture loads)
                    // - Upload mesh buffers + append to resource pool
                    // - Store gpuIndex and transition to eReady
                    //
                    // For now we keep the original sync upload logic here so rendering bring-up keeps working.

                    if (rec->cpu)
                    {
                        auto&          pool           = m_GpuResourceService->pool();
                        const uint32_t materialOffset = static_cast<uint32_t>(pool.materials.size());
                        const auto     meshEntry      = m_Registry.lookup(cmd.uuid.native());
                        for (uint32_t materialSlot = 0; materialSlot < static_cast<uint32_t>(rec->cpu->materials.size());
                             ++materialSlot)
                        {
                            const auto& mat = rec->cpu->materials[materialSlot];
                            uint32_t materialIndex = std::numeric_limits<uint32_t>::max();
                            if (!meshEntry.importedPath.empty())
                            {
                                const auto relativeMaterialPath =
                                    importedMaterialAssetRelativePath(meshEntry.importedPath, materialSlot, mat.name);
                                const auto physicalMaterialPath =
                                    (std::filesystem::path(m_Desc.assetRoot) / relativeMaterialPath).lexically_normal();
                                std::error_code ec;
                                if (std::filesystem::is_regular_file(physicalMaterialPath, ec))
                                {
                                    materialIndex = createAndAppendGpuMaterialFromAsset(
                                        m_Desc.scheme + "://" + relativeMaterialPath.generic_string(), mat);
                                }
                            }
                            if (materialIndex == std::numeric_limits<uint32_t>::max())
                                createAndAppendGpuMaterial(mat);
                        }

                        const uint32_t meshIndex = uploadMesh(*rec->cpu, materialOffset);

                        rec->gpuIndex.store(meshIndex, std::memory_order_release);
                        rec->state.store(AssetState::eReady, std::memory_order_release);

                        // Release CPU copy if not requested - but keep it for skinned meshes so the
                        // animation system can resolve the mesh's bundled skeleton (resolveSkeletonFor
                        // reads cpu->skeleton) in packaged builds, which otherwise drop CPU mesh data
                        // after GPU upload (keepCpuCopy defaults to false outside the editor).
                        if (!m_Desc.keepCpuCopy && !(rec->cpu && rec->cpu->hasSkin))
                            rec->cpu.reset();
                    }
                    else
                    {
                        rec->state.store(AssetState::eFailed, std::memory_order_release);
                    }
                }
                break;

                case UploadCmd::Kind::eTexture: {
                    auto* rec = m_TextureCache.findOrCreate(cmd.uuid);
                    if (!rec)
                        break;

                    auto st = rec->state.load(std::memory_order_acquire);
                    if (st != AssetState::eUploadQueued && st != AssetState::eCPUReady)
                        break;

                    rec->state.store(AssetState::eUploadingGPU, std::memory_order_release);

                    // TODO (deferred upload):
                    // - Transcode/prepare texture formats if needed
                    // - Upload to GPU and create bindless entry
                    // - Store gpuIndex and transition to eReady

                    if (rec->cpu)
                    {
                        const uint32_t texIndex = uploadTexture(*rec->cpu);

                        rec->gpuIndex.store(texIndex, std::memory_order_release);
                        rec->state.store(AssetState::eReady, std::memory_order_release);

                        if (!m_Desc.keepCpuCopy)
                            rec->cpu.reset();
                    }
                    else
                    {
                        rec->state.store(AssetState::eFailed, std::memory_order_release);
                    }
                }
                break;

                case UploadCmd::Kind::eGaussianSplat: {
                    auto* rec = m_GaussianSplatCache.findOrCreate(cmd.uuid);
                    if (!rec)
                        break;

                    const auto st = rec->state.load(std::memory_order_acquire);
                    if (st != AssetState::eUploadQueued && st != AssetState::eCPUReady)
                        break;

                    rec->state.store(AssetState::eUploadingGPU, std::memory_order_release);

                    if (rec->cpu)
                    {
                        const uint32_t splatIndex = uploadGaussianSplat(*rec->cpu);
                        rec->gpuIndex.store(splatIndex, std::memory_order_release);
                        rec->state.store(AssetState::eReady, std::memory_order_release);

                        if (!m_Desc.keepCpuCopy)
                            rec->cpu.reset();
                    }
                    else
                    {
                        rec->state.store(AssetState::eFailed, std::memory_order_release);
                    }
                }
                break;
            }
        }

        auto& pool = m_GpuResourceService->pool();
        pool.processDeferredFrees(frameIndex);
        if (pool.materialTableDirty)
        {
            pool.uploadMaterialTable(*m_RenderDevice);
        }

        releaseZeroRefCpuAssets(frameIndex);
    }

    void AssetSystem::releaseZeroRefCpuAssets(const uint64_t frameIndex)
    {
        if (!m_Desc.releaseZeroRefCpuAssets)
            return;

        const uint64_t idleFrames = std::max<uint64_t>(1, m_Desc.zeroRefCpuAssetIdleFrames);
        releaseZeroRefCpuAssetCache(m_SkeletonCache, frameIndex, idleFrames);
        releaseZeroRefCpuAssetCache(m_AnimationCache, frameIndex, idleFrames);
    }

    void AssetSystem::enqueueUploadOnce(UploadCmd::Kind kind, const CoreUUID& uuid, std::atomic_bool& queuedFlag)
    {
        bool expected = false;
        if (!queuedFlag.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            return;

        {
            std::scoped_lock lock(m_UploadQueueMutex);
            m_UploadQueue.push_back(UploadCmd {kind, uuid});
        }
    }

    void AssetSystem::collectFinishedCpuLoadTasks()
    {
        std::scoped_lock lock(m_CpuLoadTasksMutex);
        for (auto it = m_CpuLoadTasks.begin(); it != m_CpuLoadTasks.end();)
        {
            CpuLoadTask& cpuTask = **it;
            if (!cpuTask.done.load(std::memory_order_acquire))
            {
                ++it;
                continue;
            }

            if (cpuTask.task && m_CpuLoadScheduler)
                m_CpuLoadScheduler->wait(*cpuTask.task);
            it = m_CpuLoadTasks.erase(it);
        }
    }

    void AssetSystem::waitForCpuLoadTasks()
    {
        std::scoped_lock lock(m_CpuLoadTasksMutex);
        if (m_CpuLoadScheduler)
        {
            for (auto& cpuTask : m_CpuLoadTasks)
            {
                if (cpuTask && cpuTask->task)
                    m_CpuLoadScheduler->wait(*cpuTask->task);
            }
        }
        m_CpuLoadTasks.clear();
    }

    void AssetSystem::startMeshCpuLoadAsync(AssetRecord<vasset::VMesh, resource::GpuMesh>& rec, const CoreUUID& uuid)
    {
        std::string uri;
        if (!resolveUUIDToUri(uuid, uri))
        {
            VULTRA_CLIENT_ERROR("loadMeshAsync: cannot resolve uuid {}", uuid.toString());
            rec.state.store(AssetState::eFailed, std::memory_order_release);
            return;
        }

        auto cpuLoadTask = std::make_unique<CpuLoadTask>();
        auto* taskState  = cpuLoadTask.get();
        cpuLoadTask->task = std::make_unique<vtask::TaskSet>(1, 1, [this, &rec, uuid, uri, taskState](vtask::Range) {
            for (uint32_t attempt = 1; attempt <= kAssetLoadMaxAttempts; ++attempt)
            {
                auto br = readTextureAssetBytes(uri);
                if (!br)
                {
                    VULTRA_CLIENT_ERROR("loadMeshAsync: failed to read {} (attempt {}/{})",
                                        uri,
                                        attempt,
                                        kAssetLoadMaxAttempts);
                    continue;
                }

                auto cpu = std::make_unique<vasset::VMesh>();
                auto r   = vasset::loadMeshFromMemory(br.value(), *cpu);
                if (!r)
                {
                    VULTRA_CLIENT_ERROR("loadMeshAsync: vasset::loadMeshFromMemory failed: {} (attempt {}/{})",
                                        uri,
                                        attempt,
                                        kAssetLoadMaxAttempts);
                    continue;
                }

                rec.cpu = std::move(cpu);
                rec.state.store(AssetState::eCPUReady, std::memory_order_release);
                rec.state.store(AssetState::eUploadQueued, std::memory_order_release);
                enqueueUploadOnce(UploadCmd::Kind::eMesh, uuid, rec.uploadQueued);
                taskState->done.store(true, std::memory_order_release);
                return;
            }

            rec.state.store(AssetState::eFailed, std::memory_order_release);
            taskState->done.store(true, std::memory_order_release);
        });

        if (!m_CpuLoadScheduler)
            m_CpuLoadScheduler = std::make_unique<vtask::Scheduler>();
        m_CpuLoadScheduler->run(*cpuLoadTask->task);

        std::scoped_lock lock(m_CpuLoadTasksMutex);
        m_CpuLoadTasks.push_back(std::move(cpuLoadTask));
    }

    void AssetSystem::startTextureCpuLoadAsync(AssetRecord<vasset::VTexture, resource::GpuTexture>& rec,
                                               const CoreUUID& uuid)
    {
        std::string uri;
        if (!resolveUUIDToUri(uuid, uri))
        {
            VULTRA_CLIENT_ERROR("loadTextureAsync: cannot resolve uuid {}", uuid.toString());
            rec.state.store(AssetState::eFailed, std::memory_order_release);
            return;
        }

        auto cpuLoadTask = std::make_unique<CpuLoadTask>();
        auto* taskState  = cpuLoadTask.get();
        cpuLoadTask->task = std::make_unique<vtask::TaskSet>(1, 1, [this, &rec, uuid, uri, taskState](vtask::Range) {
            for (uint32_t attempt = 1; attempt <= kAssetLoadMaxAttempts; ++attempt)
            {
                auto br = readTextureAssetBytes(uri);
                if (!br)
                {
                    VULTRA_CLIENT_ERROR("loadTextureAsync: failed to read {} (attempt {}/{})",
                                        uri,
                                        attempt,
                                        kAssetLoadMaxAttempts);
                    continue;
                }

                auto cpu = isBuiltinTextureUri(uri) ? makeTextureFromBytes(uri, br.value()) :
                                                      std::make_unique<vasset::VTexture>();
                if (!cpu || (!isBuiltinTextureUri(uri) && !vasset::loadTextureFromMemory(br.value(), *cpu)))
                {
                    VULTRA_CLIENT_ERROR("loadTextureAsync: vasset::loadTextureFromMemory failed: {} (attempt {}/{})",
                                        uri,
                                        attempt,
                                        kAssetLoadMaxAttempts);
                    continue;
                }

                rec.cpu = std::move(cpu);
                rec.state.store(AssetState::eCPUReady, std::memory_order_release);
                rec.state.store(AssetState::eUploadQueued, std::memory_order_release);
                enqueueUploadOnce(UploadCmd::Kind::eTexture, uuid, rec.uploadQueued);
                taskState->done.store(true, std::memory_order_release);
                return;
            }

            rec.state.store(AssetState::eFailed, std::memory_order_release);
            taskState->done.store(true, std::memory_order_release);
        });

        if (!m_CpuLoadScheduler)
            m_CpuLoadScheduler = std::make_unique<vtask::Scheduler>();
        m_CpuLoadScheduler->run(*cpuLoadTask->task);

        std::scoped_lock lock(m_CpuLoadTasksMutex);
        m_CpuLoadTasks.push_back(std::move(cpuLoadTask));
    }

    void AssetSystem::startGaussianSplatCpuLoadAsync(
        AssetRecord<vasset::VGaussianSplat, resource::GpuGaussianSplat>& rec, const CoreUUID& uuid)
    {
        std::string uri;
        if (!resolveUUIDToUri(uuid, uri))
        {
            VULTRA_CLIENT_ERROR("loadGaussianSplatAsync: cannot resolve uuid {}", uuid.toString());
            rec.state.store(AssetState::eFailed, std::memory_order_release);
            return;
        }

        auto cpuLoadTask = std::make_unique<CpuLoadTask>();
        auto* taskState  = cpuLoadTask.get();
        cpuLoadTask->task = std::make_unique<vtask::TaskSet>(1, 1, [this, &rec, uuid, uri, taskState](vtask::Range) {
            for (uint32_t attempt = 1; attempt <= kAssetLoadMaxAttempts; ++attempt)
            {
                auto br = m_VFS.readAll(uri);
                if (!br)
                {
                    VULTRA_CLIENT_ERROR("loadGaussianSplatAsync: failed to read {} (attempt {}/{})",
                                        uri,
                                        attempt,
                                        kAssetLoadMaxAttempts);
                    continue;
                }

                auto cpu = std::make_unique<vasset::VGaussianSplat>();
                auto r   = vasset::loadGaussianSplatFromMemory(br.value(), *cpu);
                if (!r)
                {
                    VULTRA_CLIENT_ERROR(
                        "loadGaussianSplatAsync: vasset::loadGaussianSplatFromMemory failed: {} (attempt {}/{})",
                        uri,
                        attempt,
                        kAssetLoadMaxAttempts);
                    continue;
                }

                rec.cpu = std::move(cpu);
                rec.state.store(AssetState::eCPUReady, std::memory_order_release);
                rec.state.store(AssetState::eUploadQueued, std::memory_order_release);
                enqueueUploadOnce(UploadCmd::Kind::eGaussianSplat, uuid, rec.uploadQueued);
                taskState->done.store(true, std::memory_order_release);
                return;
            }

            rec.state.store(AssetState::eFailed, std::memory_order_release);
            taskState->done.store(true, std::memory_order_release);
        });

        if (!m_CpuLoadScheduler)
            m_CpuLoadScheduler = std::make_unique<vtask::Scheduler>();
        m_CpuLoadScheduler->run(*cpuLoadTask->task);

        std::scoped_lock lock(m_CpuLoadTasksMutex);
        m_CpuLoadTasks.push_back(std::move(cpuLoadTask));
    }

    bool AssetSystem::resolveUUIDToUri(const CoreUUID& uuid, std::string& outUri) const
    {
        if (const auto builtinUri = builtinTextureUriForUuid(uuid); !builtinUri.empty())
        {
            outUri = builtinUri;
            return true;
        }

        auto entry = m_Registry.lookup(uuid);
        if (entry.type == vasset::VAssetType::eUnknown)
            return false;

        const bool cookedOnly = entry.type == vasset::VAssetType::eMesh || entry.type == vasset::VAssetType::eTexture ||
                                entry.type == vasset::VAssetType::eSkeleton ||
                                entry.type == vasset::VAssetType::eAnimation;
        const std::string& path       = cookedOnly && !entry.importedPath.empty() ? entry.importedPath :
                                        !entry.sourcePath.empty()                 ? entry.sourcePath :
                                                                                    entry.importedPath;
        outUri                        = m_Desc.scheme + "://" + path;
        return true;
    }

    bool AssetSystem::resolveUriToUUID(std::string_view uri, CoreUUID& outUUID) const
    {
        if (isBuiltinTextureUri(uri))
        {
            outUUID = builtinTextureUuidForUri(uri);
            return true;
        }

        return m_Resolver.reverseResolve(uri, outUUID);
    }

    vbase::Result<std::vector<std::byte>, std::string> AssetSystem::readTextureAssetBytes(std::string_view uri)
    {
        if (isBuiltinTextureUri(uri))
        {
            // Packaged runtime has no builtin/ folder on disk: fall back to the mounted builtin::
            // pack (logical path = uri without the "builtin://" scheme), then to the legacy
            // embedded resource (editor .rc / linker symbols) for backward compatibility.
            const auto builtinFallback = [&]() -> vbase::Result<std::vector<std::byte>, std::string> {
                std::string_view logical = uri;
                constexpr std::string_view kScheme = "builtin://";
                if (logical.starts_with(kScheme))
                    logical.remove_prefix(kScheme.size());
                std::vector<std::byte> packed;
                if (builtin::read(logical, packed) && !packed.empty())
                    return vbase::Result<std::vector<std::byte>, std::string>::ok(std::move(packed));
                return readResourceBuiltinTextureBytes(uri);
            };

            const auto path = builtinTexturePathForUri(uri);
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file)
                return builtinFallback();

            const auto          size = static_cast<std::streamsize>(file.tellg());
            std::vector<std::byte> bytes(static_cast<size_t>(std::max<std::streamsize>(size, 0)));
            file.seekg(0);
            if (!bytes.empty() && !file.read(reinterpret_cast<char*>(bytes.data()), size))
                return builtinFallback();
            return vbase::Result<std::vector<std::byte>, std::string>::ok(std::move(bytes));
        }

        auto br = m_VFS.readAll(uri);
        if (!br)
            return vbase::Result<std::vector<std::byte>, std::string>::err("failed to read " + std::string(uri));
        return vbase::Result<std::vector<std::byte>, std::string>::ok(std::move(br.value()));
    }

    uint32_t AssetSystem::resolveBindlessTextureIndex(const CoreUUID& texUUID)
    {
        if (!texUUID.valid())
            return 0;

        auto it = m_TexUUIDToBindlessIndex.find(texUUID);
        if (it != m_TexUUIDToBindlessIndex.end())
            return it->second;

        // Load sync and register.
        auto  h   = loadTextureSync(texUUID);
        auto* rec = h.getRecord();
        if (!rec)
            return 0;

        const uint32_t idx = h.gpuIndex();
        if (idx == std::numeric_limits<uint32_t>::max())
            return 0;
        return idx;
    }

    uint32_t AssetSystem::resolveBindlessTextureIndexAsync(const CoreUUID& texUUID)
    {
        if (!texUUID.valid())
            return 0;

        auto it = m_TexUUIDToBindlessIndex.find(texUUID);
        if (it != m_TexUUIDToBindlessIndex.end())
            return it->second;

        auto h = loadTextureAsync(texUUID);
        if (!h.ready())
            return 0;

        const uint32_t idx = h.gpuIndex();
        if (idx == std::numeric_limits<uint32_t>::max())
            return 0;
        return idx;
    }

    bool AssetSystem::meshPreviewReady(const CoreUUID& meshUUID)
    {
        if (!meshUUID.valid())
            return false;

        auto handle = loadMeshAsync(meshUUID);
        if (!handle.ready())
            return false;

        if (const auto* cpuMesh = handle.cpu())
        {
            for (const auto& material : cpuMesh->materials)
            {
                if (!materialTextureDependenciesReady(material))
                    return false;
            }
        }

        return !materialRefreshPending();
    }

    bool AssetSystem::materialRefreshPending() const { return !m_PendingMaterialRefreshes.empty(); }

    bool AssetSystem::materialTextureDependenciesReady(const vasset::VMaterial& material)
    {
        auto ready = [this](const CoreUUID& uuid) {
            if (!uuid.valid())
                return true;
            const auto texture = loadTextureAsync(uuid);
            return texture.ready() && texture.gpuIndex() != std::numeric_limits<uint32_t>::max();
        };

        switch (material.model)
        {
            case vasset::VMaterialModel::ePBRMetallicRoughness:
                return ready(CoreUUID(material.core.pbrMR.baseColorTexture.uuid)) &&
                       ready(CoreUUID(material.core.pbrMR.normalTexture.uuid)) &&
                       ready(pbrMrCombinedTextureUuid(material.core.pbrMR)) &&
                       ready(CoreUUID(material.core.pbrMR.metallicTexture.uuid)) &&
                       ready(CoreUUID(material.core.pbrMR.roughnessTexture.uuid)) &&
                       ready(CoreUUID(material.core.pbrMR.ambientOcclusionTexture.uuid)) &&
                       ready(CoreUUID(material.core.pbrMR.emissiveTexture.uuid));
            case vasset::VMaterialModel::ePBRSpecularGlossiness:
                return ready(CoreUUID(material.core.pbrSG.diffuseTexture.uuid)) &&
                       ready(CoreUUID(material.core.pbrSG.specularGlossinessTexture.uuid));
            case vasset::VMaterialModel::eUnlit:
                return ready(CoreUUID(material.core.unlit.colorTexture.uuid));
            case vasset::VMaterialModel::ePhong:
            default:
                return ready(CoreUUID(material.core.phong.diffuseTexture.uuid));
        }
    }

    bool AssetSystem::refreshGpuMaterialParams(const uint32_t materialIndex, const vasset::VMaterial& material)
    {
        auto& pool = m_GpuResourceService->pool();
        if (materialIndex >= pool.materials.size())
            return true;
        if (!materialTextureDependenciesReady(material))
            return false;

        auto uploadBlock = [&](resource::GpuMaterial& gpuMaterial, const void* src, const uint32_t size) {
            if (gpuMaterial.blockOffsetBytes + size > pool.materialParams.cpu.size())
            {
                gpuMaterial.blockOffsetBytes = pool.materialParams.allocAndUpload(*m_RenderDevice, src, size, 16);
                pool.materialTableDirty      = true;
                return;
            }

            std::memcpy(pool.materialParams.cpu.data() + gpuMaterial.blockOffsetBytes, src, size);
            if (pool.materialParams.gpu)
            {
                m_RenderDevice->uploadS(*pool.materialParams.gpu,
                                         0,
                                         static_cast<uint64_t>(pool.materialParams.cpu.size()),
                                         pool.materialParams.cpu.data());
            }
        };

        auto& gpuMaterial = pool.materials[materialIndex];
        switch (material.model)
        {
            case vasset::VMaterialModel::ePBRMetallicRoughness: {
                MaterialParamsPBRMR p;
                p.baseColor       = material.core.pbrMR.baseColor;
                p.metallicFactor  = material.core.pbrMR.metallicFactor;
                p.roughnessFactor = material.core.pbrMR.roughnessFactor;
                p.alphaCutoff     = material.core.pbrMR.alphaCutoff;
                p.alphaMode       = static_cast<uint32_t>(material.core.pbrMR.alphaMode);
                p.baseColorTex    = resolveBindlessTextureIndexAsync(CoreUUID(material.core.pbrMR.baseColorTexture.uuid));
                p.normalTex       = resolveBindlessTextureIndexAsync(CoreUUID(material.core.pbrMR.normalTexture.uuid));
                p.mrTex           = resolveBindlessTextureIndexAsync(pbrMrCombinedTextureUuid(material.core.pbrMR));
                p.metallicTex     = resolveBindlessTextureIndexAsync(CoreUUID(material.core.pbrMR.metallicTexture.uuid));
                p.roughnessTex    = resolveBindlessTextureIndexAsync(CoreUUID(material.core.pbrMR.roughnessTexture.uuid));
                p.occlusionTex =
                    resolveBindlessTextureIndexAsync(CoreUUID(material.core.pbrMR.ambientOcclusionTexture.uuid));
                p.emissiveTex    = resolveBindlessTextureIndexAsync(CoreUUID(material.core.pbrMR.emissiveTexture.uuid));
                {
                    glm::vec3 emissiveColor = glm::vec3(material.core.pbrMR.emissiveColorIntensity);
                    // Texture present but factor came through black (assimp glTF quirk): emissive
                    // = factor * texture would be zero, so default the factor to white.
                    if (p.emissiveTex != 0u && emissiveColor == glm::vec3(0.0f))
                        emissiveColor = glm::vec3(1.0f);
                    p.emissiveFactor =
                        glm::vec4(emissiveColor * material.core.pbrMR.emissiveColorIntensity.a, 1.0f);
                }
                p.doubleSided    = material.core.pbrMR.doubleSided ? 1u : 0u;
                p.mrTextureMode  = static_cast<uint32_t>(pbrMrTextureMode(material.core.pbrMR));
                uploadBlock(gpuMaterial, &p, sizeof(p));
                break;
            }
            case vasset::VMaterialModel::ePBRSpecularGlossiness: {
                MaterialParamsPBRSG p;
                p.diffuseColor     = material.core.pbrSG.diffuseColor;
                p.specularFactor   = material.core.pbrSG.specularFactor;
                p.glossinessFactor = material.core.pbrSG.glossinessFactor;
                p.diffuseColorTex  = resolveBindlessTextureIndexAsync(CoreUUID(material.core.pbrSG.diffuseTexture.uuid));
                p.specularGlossinessTex =
                    resolveBindlessTextureIndexAsync(CoreUUID(material.core.pbrSG.specularGlossinessTexture.uuid));
                p.glossinessTex =
                    resolveBindlessTextureIndexAsync(CoreUUID(material.core.pbrSG.glossinessTexture.uuid));
                p.normalTex = resolveBindlessTextureIndexAsync(CoreUUID(material.core.pbrSG.normalTexture.uuid));
                uploadBlock(gpuMaterial, &p, sizeof(p));
                break;
            }
            case vasset::VMaterialModel::eUnlit: {
                MaterialParamsUnlit p;
                p.color    = material.core.unlit.color;
                p.colorTex = resolveBindlessTextureIndexAsync(CoreUUID(material.core.unlit.colorTexture.uuid));
                uploadBlock(gpuMaterial, &p, sizeof(p));
                break;
            }
            case vasset::VMaterialModel::ePhong:
            default: {
                MaterialParamsPhong p;
                p.diffuse           = material.core.phong.diffuse;
                p.specularShininess = glm::vec4(material.core.phong.specular, material.core.phong.shininess);
                p.diffuseTex        = resolveBindlessTextureIndexAsync(CoreUUID(material.core.phong.diffuseTexture.uuid));
                uploadBlock(gpuMaterial, &p, sizeof(p));
                break;
            }
        }

        m_GpuResourceService->markContentDirty();
        return true;
    }

    void AssetSystem::refreshPendingMaterialParams()
    {
        if (m_PendingMaterialRefreshes.empty())
            return;

        std::size_t checks = 0;
        std::size_t out    = 0;
        for (std::size_t i = 0; i < m_PendingMaterialRefreshes.size(); ++i)
        {
            auto& pending = m_PendingMaterialRefreshes[i];
            if (checks < kMaxMaterialRefreshChecksPerFrame)
            {
                ++checks;
                if (refreshGpuMaterialParams(pending.materialIndex, pending.material))
                    continue;
            }
            m_PendingMaterialRefreshes[out++] = std::move(pending);
        }
        m_PendingMaterialRefreshes.resize(out);
    }

    uint32_t AssetSystem::uploadTexture(const vasset::VTexture& cpuTex)
    {
        auto tr = resource::loadTextureFromVTexture(cpuTex, *m_RenderDevice);
        if (!tr)
        {
            std::string uri;
            m_Resolver.resolve(cpuTex.uuid, uri);
            VULTRA_CORE_ERROR("[AssetSystem] Failed to load texture from VTexture {}", uri);
            return 0;
        }

        resource::GpuTexture out;
        out.texture = createRef<rhi::Texture>(std::move(tr.value()));
        {
            std::string uri;
            m_Resolver.resolve(cpuTex.uuid, uri);
            const auto label = uri.empty() ? std::string("Asset Texture ") + vbase::to_string(cpuTex.uuid) :
                                             std::string("Asset Texture ") + uri;
            m_RenderDevice->updateMemoryResource(out.texture->getMemoryResourceId(), label, cpuTex.toString());
        }
        return m_GpuResourceService->createTexture(*m_RenderDevice, std::move(out));
    }

    uint32_t AssetSystem::createAndAppendGpuMaterial(const vasset::VMaterial& m)
    {
        using resource::GpuMaterial;
        using resource::GpuMaterialModel;

        GpuMaterial gm;

        // NOTE: We only support a few models now.
        // Extension/custom params can be wired through MaterialBlock later.
        // We still allocate a param block so that renderer can index.

        uint32_t blockOffset = 0;

        auto& pool = m_GpuResourceService->pool();

        auto allocBlock = [&](const void* src, uint32_t size) {
            return pool.materialParams.allocAndUpload(*m_RenderDevice, src, size, 16);
        };

        switch (m.model)
        {
            case vasset::VMaterialModel::ePBRMetallicRoughness: {
                gm.model = GpuMaterialModel::ePBRMetallicRoughness;
                MaterialParamsPBRMR p;
                p.baseColor       = m.core.pbrMR.baseColor;
                p.metallicFactor  = m.core.pbrMR.metallicFactor;
                p.roughnessFactor = m.core.pbrMR.roughnessFactor;
                p.alphaCutoff     = m.core.pbrMR.alphaCutoff;
                p.alphaMode       = static_cast<uint32_t>(m.core.pbrMR.alphaMode);
                p.baseColorTex    = resolveBindlessTextureIndexAsync(CoreUUID(m.core.pbrMR.baseColorTexture.uuid));
                p.normalTex       = resolveBindlessTextureIndexAsync(CoreUUID(m.core.pbrMR.normalTexture.uuid));
                p.mrTex           = resolveBindlessTextureIndexAsync(pbrMrCombinedTextureUuid(m.core.pbrMR));
                p.metallicTex     = resolveBindlessTextureIndexAsync(CoreUUID(m.core.pbrMR.metallicTexture.uuid));
                p.roughnessTex    = resolveBindlessTextureIndexAsync(CoreUUID(m.core.pbrMR.roughnessTexture.uuid));
                p.occlusionTex    = resolveBindlessTextureIndexAsync(CoreUUID(m.core.pbrMR.ambientOcclusionTexture.uuid));
                p.emissiveTex     = resolveBindlessTextureIndexAsync(CoreUUID(m.core.pbrMR.emissiveTexture.uuid));
                {
                    glm::vec3 emissiveColor = glm::vec3(m.core.pbrMR.emissiveColorIntensity);
                    // Texture present but factor came through black (assimp glTF quirk): emissive
                    // = factor * texture would be zero, so default the factor to white.
                    if (p.emissiveTex != 0u && emissiveColor == glm::vec3(0.0f))
                        emissiveColor = glm::vec3(1.0f);
                    p.emissiveFactor = glm::vec4(emissiveColor * m.core.pbrMR.emissiveColorIntensity.a, 1.0f);
                }
                p.doubleSided     = m.core.pbrMR.doubleSided ? 1u : 0u;
                p.mrTextureMode   = static_cast<uint32_t>(pbrMrTextureMode(m.core.pbrMR));
                blockOffset       = allocBlock(&p, sizeof(p));
                break;
            }
            case vasset::VMaterialModel::ePBRSpecularGlossiness: {
                gm.model = GpuMaterialModel::ePBRSpecularGlossiness;
                MaterialParamsPBRSG p;
                p.diffuseColor     = m.core.pbrSG.diffuseColor;
                p.specularFactor   = m.core.pbrSG.specularFactor;
                p.glossinessFactor = m.core.pbrSG.glossinessFactor;
                p.diffuseColorTex  = resolveBindlessTextureIndexAsync(CoreUUID(m.core.pbrSG.diffuseTexture.uuid));
                p.specularGlossinessTex =
                    resolveBindlessTextureIndexAsync(CoreUUID(m.core.pbrSG.specularGlossinessTexture.uuid));
                p.glossinessTex =
                    resolveBindlessTextureIndexAsync(CoreUUID(m.core.pbrSG.glossinessTexture.uuid));
                p.normalTex = resolveBindlessTextureIndexAsync(CoreUUID(m.core.pbrSG.normalTexture.uuid));
                blockOffset = allocBlock(&p, sizeof(p));
                break;
            }
            case vasset::VMaterialModel::eUnlit: {
                gm.model = GpuMaterialModel::eUnlit;
                MaterialParamsUnlit p;
                p.color     = m.core.unlit.color;
                p.colorTex  = resolveBindlessTextureIndexAsync(CoreUUID(m.core.unlit.colorTexture.uuid));
                blockOffset = allocBlock(&p, sizeof(p));
                break;
            }
            case vasset::VMaterialModel::ePhong:
            default: {
                gm.model = GpuMaterialModel::ePhong;
                MaterialParamsPhong p;
                p.diffuse           = m.core.phong.diffuse;
                p.specularShininess = glm::vec4(m.core.phong.specular, m.core.phong.shininess);
                p.diffuseTex        = resolveBindlessTextureIndexAsync(CoreUUID(m.core.phong.diffuseTexture.uuid));
                blockOffset         = allocBlock(&p, sizeof(p));
                break;
            }
        }

        gm.blockOffsetBytes = blockOffset;
        gm.tableIndex       = static_cast<uint32_t>(pool.materials.size());
        pool.materials.push_back(gm);
        if (!materialTextureDependenciesReady(m))
            m_PendingMaterialRefreshes.push_back(PendingMaterialRefresh {.materialIndex = gm.tableIndex, .material = m});
        pool.materialTableDirty = true;
        m_GpuResourceService->markContentDirty();
        return gm.tableIndex;
    }

    uint32_t AssetSystem::createAndAppendGpuMaterialFromAsset(const std::string_view uri,
                                                              const vasset::VMaterial& fallback)
    {
        using resource::GpuMaterial;
        using resource::GpuMaterialModel;

        auto textResult = loadTextAssetSync(uri);
        if (!textResult)
            return std::numeric_limits<uint32_t>::max();

        nlohmann::json doc;
        try
        {
            doc = nlohmann::json::parse(textResult.value());
        }
        catch (const std::exception& e)
        {
            VULTRA_CORE_WARN("[AssetSystem] Failed to parse material asset '{}': {}", uri, e.what());
            return std::numeric_limits<uint32_t>::max();
        }

        const auto parsed = material::materialAssetFromJson(doc);
        if (!parsed.ok() || parsed.asset.source.kind != material::MaterialSourceKind::eBuiltin ||
            parsed.asset.source.id != "builtin/pbr")
        {
            for (const auto& diagnostic : parsed.diagnostics)
                VULTRA_CORE_WARN("[AssetSystem] Material asset '{}': {}", uri, diagnostic);
            return std::numeric_limits<uint32_t>::max();
        }

        MaterialParamsPBRMR p;
        if (fallback.model == vasset::VMaterialModel::ePBRMetallicRoughness)
        {
            p.baseColor       = fallback.core.pbrMR.baseColor;
            p.metallicFactor  = fallback.core.pbrMR.metallicFactor;
            p.roughnessFactor = fallback.core.pbrMR.roughnessFactor;
            p.alphaCutoff     = fallback.core.pbrMR.alphaCutoff;
            p.alphaMode       = static_cast<uint32_t>(fallback.core.pbrMR.alphaMode);
            p.baseColorTex    = resolveBindlessTextureIndexAsync(CoreUUID(fallback.core.pbrMR.baseColorTexture.uuid));
            p.normalTex       = resolveBindlessTextureIndexAsync(CoreUUID(fallback.core.pbrMR.normalTexture.uuid));
            p.mrTex           = resolveBindlessTextureIndexAsync(pbrMrCombinedTextureUuid(fallback.core.pbrMR));
            p.metallicTex     = resolveBindlessTextureIndexAsync(CoreUUID(fallback.core.pbrMR.metallicTexture.uuid));
            p.roughnessTex    = resolveBindlessTextureIndexAsync(CoreUUID(fallback.core.pbrMR.roughnessTexture.uuid));
            p.occlusionTex =
                resolveBindlessTextureIndexAsync(CoreUUID(fallback.core.pbrMR.ambientOcclusionTexture.uuid));
            p.emissiveTex   = resolveBindlessTextureIndexAsync(CoreUUID(fallback.core.pbrMR.emissiveTexture.uuid));
            p.emissiveFactor = glm::vec4(glm::vec3(fallback.core.pbrMR.emissiveColorIntensity) *
                                             fallback.core.pbrMR.emissiveColorIntensity.a,
                                         1.0f);
            p.doubleSided   = fallback.core.pbrMR.doubleSided ? 1u : 0u;
            p.mrTextureMode = static_cast<uint32_t>(pbrMrTextureMode(fallback.core.pbrMR));
        }

        auto textureIndexForUri = [&](const nlohmann::json& properties, const char* key) {
            if (!properties.contains(key))
                return 0u;
            const auto textureUri = jsonStringValue(properties[key]);
            if (!textureUri || textureUri->empty())
                return 0u;
            CoreUUID uuid;
            if (!resolveUriToUUID(*textureUri, uuid))
                return 0u;
            return resolveBindlessTextureIndexAsync(uuid);
        };

        const auto* properties = doc.contains("properties") && doc["properties"].is_object() ? &doc["properties"] :
                                                                                                  nullptr;
        if (properties)
        {
            if (properties->contains("baseColor"))
            {
                if (const auto value = jsonVec4Value((*properties)["baseColor"]))
                    p.baseColor = *value;
            }
            if (properties->contains("metallic"))
            {
                if (const auto value = jsonFloatValue((*properties)["metallic"]))
                    p.metallicFactor = *value;
            }
            if (properties->contains("roughness"))
            {
                if (const auto value = jsonFloatValue((*properties)["roughness"]))
                    p.roughnessFactor = *value;
            }
            if (properties->contains("alphaCutoff"))
            {
                if (const auto value = jsonFloatValue((*properties)["alphaCutoff"]))
                    p.alphaCutoff = *value;
            }
            if (properties->contains("alphaMode"))
            {
                if (const auto value = jsonStringValue((*properties)["alphaMode"]))
                    p.alphaMode = alphaModeFromString(*value);
            }
            if (properties->contains("doubleSided"))
            {
                if (const auto value = jsonBoolValue((*properties)["doubleSided"]))
                    p.doubleSided = *value ? 1u : 0u;
            }
            {
                glm::vec3 emissiveColor {glm::vec3(p.emissiveFactor)};
                float     emissiveStrength {1.0f};
                if (properties->contains("emissiveColor"))
                {
                    if (const auto value = jsonVec4Value((*properties)["emissiveColor"]))
                        emissiveColor = glm::vec3(*value);
                }
                if (properties->contains("emissiveStrength"))
                {
                    if (const auto value = jsonFloatValue((*properties)["emissiveStrength"]))
                        emissiveStrength = *value;
                }
                p.emissiveFactor = glm::vec4(emissiveColor * emissiveStrength, 1.0f);
            }

            p.baseColorTex = textureIndexForUri(*properties, "baseColorTexture");
            p.normalTex    = textureIndexForUri(*properties, "normalTexture");
            p.mrTex        = textureIndexForUri(*properties, "metallicRoughnessTexture");
            p.metallicTex  = textureIndexForUri(*properties, "metallicTexture");
            p.roughnessTex = textureIndexForUri(*properties, "roughnessTexture");
            p.occlusionTex = textureIndexForUri(*properties, "ambientOcclusionTexture");
            p.emissiveTex  = textureIndexForUri(*properties, "emissiveTexture");
            p.mrTextureMode =
                p.mrTex != 0u ? static_cast<uint32_t>(PbrMrTextureMode::eGltfMetallicRoughness) : 0u;
        }

        auto& pool = m_GpuResourceService->pool();

        GpuMaterial gm;
        gm.model            = GpuMaterialModel::ePBRMetallicRoughness;
        gm.blockOffsetBytes = pool.materialParams.allocAndUpload(*m_RenderDevice, &p, sizeof(p), 16);
        gm.tableIndex       = static_cast<uint32_t>(pool.materials.size());
        pool.materials.push_back(gm);
        pool.materialTableDirty = true;
        m_GpuResourceService->markContentDirty();
        return gm.tableIndex;
    }

    void AssetSystem::emitImportedMaterialAssets(const std::string_view sourceRelativePath)
    {
#ifdef VULTRA_HAS_VASSET_IMPORT
        auto materialUriForRef = [&](const vasset::VTextureRef& ref) -> std::string {
            const CoreUUID uuid(ref.uuid);
            if (!uuid.valid())
                return {};
            std::string uri;
            return resolveUUIDToUri(uuid, uri) ? uri : std::string {};
        };

        auto addTextureProperty = [&](nlohmann::json& properties, const char* key, const vasset::VTextureRef& ref) {
            if (const auto uri = materialUriForRef(ref); !uri.empty())
                properties[key] = uri;
        };

        auto makeMaterialJson = [&](const vasset::VMaterial& material, const uint32_t slot) {
            nlohmann::json properties = nlohmann::json::object();
            switch (material.model)
            {
                case vasset::VMaterialModel::eUnlit:
                    properties["baseColor"] = jsonVec4(material.core.unlit.color);
                    properties["metallic"]  = 0.0f;
                    properties["roughness"] = 1.0f;
                    addTextureProperty(properties, "baseColorTexture", material.core.unlit.colorTexture);
                    break;
                case vasset::VMaterialModel::ePBRSpecularGlossiness:
                    properties["baseColor"] = jsonVec4(material.core.pbrSG.diffuseColor);
                    properties["metallic"]  = 0.0f;
                    properties["roughness"] = std::clamp(1.0f - material.core.pbrSG.glossinessFactor, 0.045f, 1.0f);
                    addTextureProperty(properties, "baseColorTexture", material.core.pbrSG.diffuseTexture);
                    break;
                case vasset::VMaterialModel::ePhong:
                    properties["baseColor"] = jsonVec4(material.core.phong.diffuse);
                    properties["metallic"]  = 0.0f;
                    properties["roughness"] = roughnessFromPhongShininess(material.core.phong.shininess);
                    addTextureProperty(properties, "baseColorTexture", material.core.phong.diffuseTexture);
                    addTextureProperty(properties, "normalTexture", material.core.phong.normalTexture);
                    addTextureProperty(properties, "emissiveTexture", material.core.phong.emissiveTexture);
                    break;
                case vasset::VMaterialModel::ePBRMetallicRoughness:
                default:
                    properties["baseColor"]   = jsonVec4(material.core.pbrMR.baseColor);
                    properties["metallic"]    = material.core.pbrMR.metallicFactor;
                    properties["roughness"]   = material.core.pbrMR.roughnessFactor;
                    properties["alphaCutoff"] = material.core.pbrMR.alphaCutoff;
                    properties["alphaMode"]   = alphaModeString(material.core.pbrMR.alphaMode);
                    properties["doubleSided"] = material.core.pbrMR.doubleSided;
                    addTextureProperty(properties, "baseColorTexture", material.core.pbrMR.baseColorTexture);
                    addTextureProperty(properties, "normalTexture", material.core.pbrMR.normalTexture);
                    addTextureProperty(properties, "metallicTexture", material.core.pbrMR.metallicTexture);
                    addTextureProperty(properties, "roughnessTexture", material.core.pbrMR.roughnessTexture);
                    addTextureProperty(
                        properties, "metallicRoughnessTexture", material.core.pbrMR.metallicRoughnessTexture);
                    addTextureProperty(
                        properties, "ambientOcclusionTexture", material.core.pbrMR.ambientOcclusionTexture);
                    addTextureProperty(properties, "emissiveTexture", material.core.pbrMR.emissiveTexture);
                    break;
            }

            const auto name = material.name.empty() ? std::string("Material ") + std::to_string(slot) :
                                                      material.name;
            return nlohmann::json {
                {"type", "Material"},
                {"version", 1},
                {"name", name},
                {"source", {{"kind", "builtin"}, {"id", "builtin/pbr"}}},
                {"properties", properties},
            };
        };

        const auto source = std::string(sourceRelativePath);
        const auto sourceMeshPrefix = source + "#mesh/";
        for (const auto& [uuidText, entry] : m_Registry.getRegistry())
        {
            static_cast<void>(uuidText);
            if (entry.type != vasset::VAssetType::eMesh || entry.importedPath.empty())
                continue;
            if (entry.sourcePath != source && !entry.sourcePath.starts_with(sourceMeshPrefix))
                continue;

            vasset::VMesh mesh;
            const auto meshPath = (std::filesystem::path(m_Desc.assetRoot) / entry.importedPath).lexically_normal();
            if (!vasset::loadMesh(meshPath.generic_string(), mesh))
            {
                VULTRA_CORE_WARN("[AssetSystem] Failed to read imported mesh while emitting material assets: {}",
                                 meshPath.generic_string());
                continue;
            }

            for (uint32_t slot = 0; slot < static_cast<uint32_t>(mesh.materials.size()); ++slot)
            {
                const auto& material = mesh.materials[slot];
                const auto  relative = importedMaterialAssetRelativePath(entry.importedPath, slot, material.name);
                const auto  physical = (std::filesystem::path(m_Desc.assetRoot) / relative).lexically_normal();

                std::error_code ec;
                std::filesystem::create_directories(physical.parent_path(), ec);
                if (ec)
                {
                    VULTRA_CORE_WARN("[AssetSystem] Failed to create imported material asset folder '{}': {}",
                                     physical.parent_path().generic_string(),
                                     ec.message());
                    continue;
                }

                std::ofstream file(physical, std::ios::binary | std::ios::trunc);
                if (!file)
                {
                    VULTRA_CORE_WARN("[AssetSystem] Failed to write imported material asset: {}",
                                     physical.generic_string());
                    continue;
                }
                file << makeMaterialJson(material, slot).dump(2) << '\n';
            }
        }
#else
        static_cast<void>(sourceRelativePath);
#endif
    }

    uint32_t AssetSystem::uploadMesh(const vasset::VMesh& cpuMesh, uint32_t materialOffset)
    {
        if (!m_RenderDevice)
            return std::numeric_limits<uint32_t>::max();

        auto& pool = m_GpuResourceService->pool();

        uint32_t strideBytes = 0;
        auto     attrs       = buildVertexAttributes(cpuMesh.vertexFlags, strideBytes);

        // Pack CPU mesh into an AoS byte stream based on vertex flags.
        auto vertexData = packVertices(cpuMesh, {.stride = strideBytes, .attributes = attrs});

        GpuMeshCreateDesc desc;
        desc.vertexData        = vertexData.data();
        desc.vertexDataBytes   = static_cast<uint64_t>(vertexData.size());
        desc.vertexCount       = cpuMesh.vertexCount;
        desc.vertexAttributes  = attrs;
        desc.vertexStrideBytes = strideBytes;

        if (!cpuMesh.indices.empty())
        {
            desc.indexData  = cpuMesh.indices.data();
            desc.indexCount = static_cast<uint32_t>(cpuMesh.indices.size());
            desc.indexType  = rhi::IndexType::eUInt32;
        }

        std::vector<resource::GpuMeshlet> gpuMeshlets;
        std::vector<uint32_t>             gpuMeshletVertices;
        std::vector<uint32_t>             gpuMeshletTriangles;

        for (const auto& subMesh : cpuMesh.subMeshes)
        {
            const uint32_t subVertexBase     = subMesh.vertexOffset;
            const uint32_t subMaterialIndex  = materialOffset + subMesh.materialIndex;
            const uint32_t meshletVertexBase = static_cast<uint32_t>(gpuMeshletVertices.size());
            const uint32_t meshletTriBase    = static_cast<uint32_t>(gpuMeshletTriangles.size());

            for (uint32_t v : subMesh.meshletGroup.meshletVertices)
                gpuMeshletVertices.push_back(subVertexBase + v);
            for (uint8_t tri : subMesh.meshletGroup.meshletTriangles)
                gpuMeshletTriangles.push_back(static_cast<uint32_t>(tri));

            for (const auto& ml : subMesh.meshletGroup.meshlets)
            {
                resource::GpuMeshlet gm {};
                gm.vertexOffset   = meshletVertexBase + ml.vertexOffset;
                gm.vertexCount    = ml.vertexCount;
                gm.triangleOffset = meshletTriBase + ml.triangleOffset;
                gm.triangleCount  = ml.triangleCount;
                gm.materialIndex  = materialOffset + ml.materialIndex;
                gm.center         = ml.center;
                gm.radius         = ml.radius;
                gm.coneAxis       = ml.coneAxis;
                gm.coneCutoff     = ml.coneCutoff;
                gm.coneApex       = ml.coneApex;
                if (subMesh.meshletGroup.meshlets.empty())
                    gm.materialIndex = subMaterialIndex;
                gpuMeshlets.push_back(gm);
            }
        }

        desc.meshletData          = gpuMeshlets.data();
        desc.meshletCount         = static_cast<uint32_t>(gpuMeshlets.size());
        desc.meshletVertexData    = gpuMeshletVertices.data();
        desc.meshletVertexCount   = static_cast<uint32_t>(gpuMeshletVertices.size());
        desc.meshletTriangleData  = gpuMeshletTriangles.data();
        desc.meshletTriangleCount = static_cast<uint32_t>(gpuMeshletTriangles.size());

        // Asset meshes are generally useful in both CPU-driven and GPU-driven passes.
        desc.usage = GpuMeshUsageFlags::eAll;

        const uint32_t meshIndex = m_GpuResourceService->createMesh(*m_RenderDevice, desc);
        if (meshIndex == std::numeric_limits<uint32_t>::max())
            return meshIndex;

        pool.meshes[meshIndex].materialOffset = materialOffset;
        pool.meshes[meshIndex].materialCount  = static_cast<uint32_t>(cpuMesh.materials.size());
        pool.meshes[meshIndex].subMeshes.clear();
        pool.meshes[meshIndex].subMeshes.reserve(std::max<size_t>(cpuMesh.subMeshes.size(), 1u));
        for (const auto& subMesh : cpuMesh.subMeshes)
        {
            resource::GpuSubMesh gpuSubMesh {};
            gpuSubMesh.vertexOffset  = subMesh.vertexOffset;
            gpuSubMesh.vertexCount   = subMesh.vertexCount;
            gpuSubMesh.indexOffset   = subMesh.indexOffset;
            gpuSubMesh.indexCount    = subMesh.indexCount;
            gpuSubMesh.materialIndex = materialOffset + subMesh.materialIndex;
            pool.meshes[meshIndex].subMeshes.push_back(gpuSubMesh);
        }
        if (pool.meshes[meshIndex].subMeshes.empty() && cpuMesh.vertexCount > 0 && !cpuMesh.indices.empty())
        {
            pool.meshes[meshIndex].subMeshes.push_back(resource::GpuSubMesh {
                .vertexOffset  = 0u,
                .vertexCount   = cpuMesh.vertexCount,
                .indexOffset   = 0u,
                .indexCount    = static_cast<uint32_t>(cpuMesh.indices.size()),
                .materialIndex = materialOffset,
            });
        }
        pool.meshes[meshIndex].hasSkin = cpuMesh.hasSkin;
        pool.meshes[meshIndex].skeleton = CoreUUID(cpuMesh.skeleton);
        pool.meshes[meshIndex].inverseBindPoses = cpuMesh.inverseBindPoses;

        const bool rayTracingEnabled =
            HasFlagValues(m_RenderDevice->getFeatureFlag(), rhi::RenderDeviceFeatureFlagBits::eRayTracingPipeline);
        if (rayTracingEnabled && pool.meshes[meshIndex].vertexBuffer && pool.meshes[meshIndex].indexBuffer &&
            !pool.meshes[meshIndex].subMeshes.empty())
        {
            auto& gpuMesh = pool.meshes[meshIndex];

            const auto vertexAddress    = m_RenderDevice->getBufferDeviceAddress(gpuMesh.vertexBuffer);
            const auto indexAddress     = m_RenderDevice->getBufferDeviceAddress(gpuMesh.indexBuffer);
            gpuMesh.vertexBufferAddress = vertexAddress;
            gpuMesh.indexBufferAddress  = indexAddress;
            const auto     positionIt   = gpuMesh.vertexAttributes.find(0);
            const uint32_t positionOffsetBytes =
                positionIt != gpuMesh.vertexAttributes.end() ? positionIt->second.offset : 0u;

            std::vector<rhi::RenderSubMesh> rtSubMeshes;
            rtSubMeshes.reserve(gpuMesh.subMeshes.size());
            for (const auto& sm : gpuMesh.subMeshes)
            {
                if (sm.indexCount == 0)
                    continue;

                rhi::RenderSubMesh rtSubMesh {};
                rtSubMesh.vertexBufferAddress = vertexAddress;
                rtSubMesh.indexBufferAddress =
                    rhi::DeviceAddress {indexAddress.value + static_cast<uint64_t>(sm.indexOffset) * sizeof(uint32_t)};
                rtSubMesh.vertexStride        = gpuMesh.vertexStrideBytes;
                rtSubMesh.vertexCount         = gpuMesh.vertexCount;
                rtSubMesh.vertexOffset        = sm.vertexOffset;
                rtSubMesh.positionOffsetBytes = positionOffsetBytes;
                rtSubMesh.indexCount          = sm.indexCount;
                rtSubMesh.indexType           = rhi::IndexType::eUInt32;
                rtSubMesh.materialIndex       = sm.materialIndex;
                const uint32_t localMaterialIndex =
                    sm.materialIndex >= materialOffset ? sm.materialIndex - materialOffset : sm.materialIndex;
                rtSubMesh.opaque = localMaterialIndex >= cpuMesh.materials.size() ||
                                   !materialNeedsAnyHit(cpuMesh.materials[localMaterialIndex]);
                rtSubMeshes.push_back(rtSubMesh);
            }

            if (!rtSubMeshes.empty())
                gpuMesh.blas = m_RenderDevice->createBuildRenderMeshBLAS(rtSubMeshes);
        }

        // Remap meshlet vertex indices from local mesh space to global packed-vertex space.
        auto&          gpuMesh = pool.meshes[meshIndex];
        const uint32_t globalBaseVertex =
            gpuMesh.vertexStrideBytes > 0 ? (gpuMesh.vertexByteOffset / gpuMesh.vertexStrideBytes) : 0u;
        for (uint32_t i = 0; i < gpuMesh.meshletCount; ++i)
        {
            auto& gm = pool.meshlets.cpuMeshlets[gpuMesh.meshletOffset + i];
            for (uint32_t j = 0; j < gm.vertexCount; ++j)
                pool.meshlets.cpuMeshletVertices[gm.vertexOffset + j] += globalBaseVertex;
        }
        if (pool.meshlets.meshletVerticesBuffer && !pool.meshlets.cpuMeshletVertices.empty())
            m_RenderDevice->uploadS(*pool.meshlets.meshletVerticesBuffer,
                                    0,
                                    pool.meshlets.cpuMeshletVertices.size() * sizeof(uint32_t),
                                    pool.meshlets.cpuMeshletVertices.data());

        m_GpuResourceService->markContentDirty();

        return meshIndex;
    }

    uint32_t AssetSystem::uploadGaussianSplat(const vasset::VGaussianSplat& cpuSplat)
    {
        if (!m_RenderDevice)
            return std::numeric_limits<uint32_t>::max();

        auto& pool = m_GpuResourceService->pool();

        constexpr float kShC0          = 0.28209479177f;
        constexpr int   kTargetRest    = static_cast<int>(resource::GpuGaussianSplat::s_PackedShRestCoeffs);
        constexpr float kAlphaMinKeep  = 0.001f;
        constexpr float kAlphaLogitMin = -20.0f;
        constexpr float kAlphaLogitMax = 20.0f;
        constexpr float kLogScaleMin   = -20.0f;
        constexpr float kLogScaleMax   = 4.0f;

        const int fileDegree     = std::clamp(cpuSplat.shDegree, 0, 3);
        const int fileRestCoeffs = fileDegree > 0 ? (((fileDegree + 1) * (fileDegree + 1)) - 1) : 0;

        auto decodeAlpha = [&](const vasset::VGaussianSplatPoint& p) -> float {
            if (!std::isfinite(p.opacity))
                return 0.0f;

            return sigmoid(std::clamp(p.opacity, kAlphaLogitMin, kAlphaLogitMax));
        };

        auto decodeScaleLin = [&](const vasset::VGaussianSplatPoint& p) -> glm::vec3 {
            if (!std::isfinite(p.scale.x) || !std::isfinite(p.scale.y) || !std::isfinite(p.scale.z))
                return glm::vec3(1e-6f);

            return glm::vec3(std::exp(std::clamp(p.scale.x, kLogScaleMin, kLogScaleMax)),
                             std::exp(std::clamp(p.scale.y, kLogScaleMin, kLogScaleMax)),
                             std::exp(std::clamp(p.scale.z, kLogScaleMin, kLogScaleMax)));
        };

        auto decodeBaseRgb = [&](const vasset::VGaussianSplatPoint& p) -> glm::vec3 {
            if (!std::isfinite(p.shDC.x) || !std::isfinite(p.shDC.y) || !std::isfinite(p.shDC.z))
                return glm::vec3(0.0f);

            return glm::clamp(kShC0 * p.shDC + glm::vec3(0.5f), glm::vec3(0.0f), glm::vec3(1.0f));
        };

        std::vector<glm::vec4>  packedCenters;
        std::vector<glm::vec4>  packedScales;
        std::vector<glm::uvec4> packedCovariances;
        std::vector<glm::uvec2> packedColors;
        std::vector<glm::uvec2> packedSh;

        packedCenters.reserve(cpuSplat.splats.size());
        packedScales.reserve(cpuSplat.splats.size());
        packedCovariances.reserve(cpuSplat.splats.size());
        packedColors.reserve(cpuSplat.splats.size());
        packedSh.reserve(cpuSplat.splats.size() * resource::GpuGaussianSplat::s_PackedShRestCoeffs);

        for (size_t i = 0; i < cpuSplat.splats.size(); ++i)
        {
            const auto& p = cpuSplat.splats[i];
            if (!std::isfinite(p.position.x) || !std::isfinite(p.position.y) || !std::isfinite(p.position.z))
                continue;

            const float alpha = decodeAlpha(p);
            if (alpha < kAlphaMinKeep)
                continue;

            packedCenters.push_back(glm::vec4(p.position, 1.0f));

            const glm::vec3 baseRgb = decodeBaseRgb(p);
            packedColors.emplace_back(packF16x2Clamp01(baseRgb.r, baseRgb.g), packF16x2Clamp01(baseRgb.b, alpha));

            const glm::vec3 scaleLin = decodeScaleLin(p);
            packedScales.push_back(glm::vec4(scaleLin, 0.0f));
            const glm::quat q = sanitizeAndNormalizeQuatFromExternalXyzw(p.rotation);
            const glm::mat3 R = glm::mat3_cast(q);

            glm::mat3 D(0.0f);
            D[0][0] = scaleLin.x * scaleLin.x;
            D[1][1] = scaleLin.y * scaleLin.y;
            D[2][2] = scaleLin.z * scaleLin.z;

            const glm::mat3 Sigma = R * D * glm::transpose(R);

            const float m11 = Sigma[0][0];
            const float m12 = Sigma[1][0];
            const float m13 = Sigma[2][0];
            const float m22 = Sigma[1][1];
            const float m23 = Sigma[2][1];
            const float m33 = Sigma[2][2];

            packedCovariances.emplace_back(packF16x2(m11, m12), packF16x2(m13, m22), packF16x2(m23, m33), 0u);

            const size_t pointBase = i * static_cast<size_t>(fileRestCoeffs) * 3ull;
            for (int k = 0; k < kTargetRest; ++k)
            {
                float rr = 0.0f;
                float gg = 0.0f;
                float bb = 0.0f;
                if (fileRestCoeffs > 0 && k < fileRestCoeffs)
                {
                    const size_t coeffBase = pointBase + static_cast<size_t>(k) * 3ull;
                    if (coeffBase + 2ull < cpuSplat.sh.size())
                    {
                        rr = cpuSplat.sh[coeffBase + 0ull];
                        gg = cpuSplat.sh[coeffBase + 1ull];
                        bb = cpuSplat.sh[coeffBase + 2ull];
                    }

                    if (!std::isfinite(rr))
                        rr = 0.0f;
                    if (!std::isfinite(gg))
                        gg = 0.0f;
                    if (!std::isfinite(bb))
                        bb = 0.0f;

                    rr = std::clamp(rr, -10.0f, 10.0f);
                    gg = std::clamp(gg, -10.0f, 10.0f);
                    bb = std::clamp(bb, -10.0f, 10.0f);
                }

                packedSh.emplace_back(packF16x2(rr, gg), packF16x2(bb, 0.0f));
            }
        }

        if (packedCenters.empty())
        {
            VULTRA_CORE_ERROR("[AssetSystem] uploadGaussianSplat: no valid points in '{}'.", cpuSplat.name);
            return std::numeric_limits<uint32_t>::max();
        }

        glm::vec3 center(0.0f);
        glm::vec3 minP(std::numeric_limits<float>::infinity());
        glm::vec3 maxP(-std::numeric_limits<float>::infinity());
        for (const auto& c : packedCenters)
        {
            minP = glm::min(minP, glm::vec3(c));
            maxP = glm::max(maxP, glm::vec3(c));
        }
        center = 0.5f * (minP + maxP);

        float radius = 0.0f;
        for (const auto& c : packedCenters)
            radius = std::max(radius, glm::length(glm::vec3(c) - center));

        resource::GpuGaussianSplat out;
        out.pointCount  = static_cast<uint32_t>(packedCenters.size());
        out.shDegree    = fileDegree;
        out.center      = center;
        out.radius      = radius;
        out.pointOffset = pool.gaussianStorage.appendCenters(*m_RenderDevice, packedCenters.data(), out.pointCount);
        pool.gaussianStorage.appendScales(*m_RenderDevice, packedScales.data(), out.pointCount);
        pool.gaussianStorage.appendCovariances(*m_RenderDevice, packedCovariances.data(), out.pointCount);
        pool.gaussianStorage.appendColors(*m_RenderDevice, packedColors.data(), out.pointCount);
        pool.gaussianStorage.appendSh(*m_RenderDevice, packedSh.data(), static_cast<uint32_t>(packedSh.size()));

        const uint32_t index = static_cast<uint32_t>(pool.gaussianSplats.size());
        pool.gaussianSplats.push_back(std::move(out));
        pool.uploadGaussianSplatMeta(*m_RenderDevice);
        m_GpuResourceService->markContentDirty();
        return index;
    }

    AssetHandle<vasset::VTexture, resource::GpuTexture> AssetSystem::loadTextureAsync(const CoreUUID& uuid)
    {
        if (!m_Desc.asyncLoading)
            return loadTextureSync(uuid);

        auto* rec = m_TextureCache.findOrCreate(uuid);
        if (!rec)
            return {};
        markAssetUsed(*rec, m_LastUpdateFrame);

        auto st = rec->state.load(std::memory_order_acquire);
        if (st == AssetState::eUnloaded)
        {
            AssetState expected = AssetState::eUnloaded;
            if (rec->state.compare_exchange_strong(
                    expected, AssetState::eLoadingCPU, std::memory_order_acq_rel, std::memory_order_acquire))
            {
                startTextureCpuLoadAsync(*rec, uuid);
            }
        }
        else if (st == AssetState::eCPUReady)
        {
            rec->state.store(AssetState::eUploadQueued, std::memory_order_release);
            enqueueUploadOnce(UploadCmd::Kind::eTexture, uuid, rec->uploadQueued);
        }

        return AssetHandle<vasset::VTexture, resource::GpuTexture>(rec);
    }

    AssetHandle<vasset::VMesh, resource::GpuMesh> AssetSystem::loadMeshAsync(const CoreUUID& uuid)
    {
        if (!m_Desc.asyncLoading)
            return loadMeshSync(uuid);

        auto* rec = m_MeshCache.findOrCreate(uuid);
        if (!rec)
            return {};
        markAssetUsed(*rec, m_LastUpdateFrame);

        auto st = rec->state.load(std::memory_order_acquire);
        if (st == AssetState::eUnloaded)
        {
            AssetState expected = AssetState::eUnloaded;
            if (rec->state.compare_exchange_strong(
                    expected, AssetState::eLoadingCPU, std::memory_order_acq_rel, std::memory_order_acquire))
            {
                startMeshCpuLoadAsync(*rec, uuid);
            }
        }
        else if (st == AssetState::eCPUReady)
        {
            rec->state.store(AssetState::eUploadQueued, std::memory_order_release);
            enqueueUploadOnce(UploadCmd::Kind::eMesh, uuid, rec->uploadQueued);
        }

        return AssetHandle<vasset::VMesh, resource::GpuMesh>(rec);
    }

    AssetHandle<vasset::VGaussianSplat, resource::GpuGaussianSplat>
    AssetSystem::loadGaussianSplatAsync(const CoreUUID& uuid)
    {
        if (!m_Desc.asyncLoading)
            return loadGaussianSplatSync(uuid);

        auto* rec = m_GaussianSplatCache.findOrCreate(uuid);
        if (!rec)
            return {};
        markAssetUsed(*rec, m_LastUpdateFrame);

        auto st = rec->state.load(std::memory_order_acquire);
        if (st == AssetState::eUnloaded)
        {
            AssetState expected = AssetState::eUnloaded;
            if (rec->state.compare_exchange_strong(
                    expected, AssetState::eLoadingCPU, std::memory_order_acq_rel, std::memory_order_acquire))
            {
                startGaussianSplatCpuLoadAsync(*rec, uuid);
            }
        }
        else if (st == AssetState::eCPUReady)
        {
            rec->state.store(AssetState::eUploadQueued, std::memory_order_release);
            enqueueUploadOnce(UploadCmd::Kind::eGaussianSplat, uuid, rec->uploadQueued);
        }

        return AssetHandle<vasset::VGaussianSplat, resource::GpuGaussianSplat>(rec);
    }

    AssetHandle<vasset::VSkeleton, resource::CpuAsset> AssetSystem::loadSkeletonAsync(const CoreUUID& uuid)
    {
        return loadSkeletonSync(uuid);
    }

    AssetHandle<vasset::VAnimation, resource::CpuAsset> AssetSystem::loadAnimationAsync(const CoreUUID& uuid)
    {
        return loadAnimationSync(uuid);
    }

    AssetHandle<vasset::VSkeleton, resource::CpuAsset> AssetSystem::loadSkeletonSync(const CoreUUID& uuid)
    {
        auto* rec = m_SkeletonCache.findOrCreate(uuid);
        if (!rec)
            return {};
        markAssetUsed(*rec, m_LastUpdateFrame);

        if (rec->state.load(std::memory_order_acquire) == AssetState::eReady)
            return AssetHandle<vasset::VSkeleton, resource::CpuAsset>(rec);

        if (rec->state.load(std::memory_order_acquire) == AssetState::eUnloaded)
        {
            rec->state.store(AssetState::eLoadingCPU, std::memory_order_release);

            std::string uri;
            if (!resolveUUIDToUri(uuid, uri))
            {
                VULTRA_CLIENT_ERROR("loadSkeletonSync: cannot resolve uuid {}", uuid.toString());
                rec->state.store(AssetState::eFailed, std::memory_order_release);
                return AssetHandle<vasset::VSkeleton, resource::CpuAsset>(rec);
            }

            auto br = m_VFS.readAll(uri);
            if (!br)
            {
                VULTRA_CLIENT_ERROR("loadSkeletonSync: failed to read {}", uri);
                rec->state.store(AssetState::eFailed, std::memory_order_release);
                return AssetHandle<vasset::VSkeleton, resource::CpuAsset>(rec);
            }

            auto cpu = std::make_unique<vasset::VSkeleton>();
            auto r   = vasset::loadSkeletonFromMemory(br.value(), *cpu);
            if (!r)
            {
                VULTRA_CLIENT_ERROR("loadSkeletonSync: vasset::loadSkeletonFromMemory failed: {}", uri);
                rec->state.store(AssetState::eFailed, std::memory_order_release);
                return AssetHandle<vasset::VSkeleton, resource::CpuAsset>(rec);
            }

            rec->cpu = std::move(cpu);
            rec->gpuIndex.store(0u, std::memory_order_release);
            rec->state.store(AssetState::eReady, std::memory_order_release);
        }

        return AssetHandle<vasset::VSkeleton, resource::CpuAsset>(rec);
    }

    AssetHandle<vasset::VAnimation, resource::CpuAsset> AssetSystem::loadAnimationSync(const CoreUUID& uuid)
    {
        auto* rec = m_AnimationCache.findOrCreate(uuid);
        if (!rec)
            return {};
        markAssetUsed(*rec, m_LastUpdateFrame);

        if (rec->state.load(std::memory_order_acquire) == AssetState::eReady)
            return AssetHandle<vasset::VAnimation, resource::CpuAsset>(rec);

        if (rec->state.load(std::memory_order_acquire) == AssetState::eUnloaded)
        {
            rec->state.store(AssetState::eLoadingCPU, std::memory_order_release);

            std::string uri;
            if (!resolveUUIDToUri(uuid, uri))
            {
                VULTRA_CLIENT_ERROR("loadAnimationSync: cannot resolve uuid {}", uuid.toString());
                rec->state.store(AssetState::eFailed, std::memory_order_release);
                return AssetHandle<vasset::VAnimation, resource::CpuAsset>(rec);
            }

            auto br = m_VFS.readAll(uri);
            if (!br)
            {
                VULTRA_CLIENT_ERROR("loadAnimationSync: failed to read {}", uri);
                rec->state.store(AssetState::eFailed, std::memory_order_release);
                return AssetHandle<vasset::VAnimation, resource::CpuAsset>(rec);
            }

            auto cpu = std::make_unique<vasset::VAnimation>();
            auto r   = vasset::loadAnimationFromMemory(br.value(), *cpu);
            if (!r)
            {
                VULTRA_CLIENT_ERROR("loadAnimationSync: vasset::loadAnimationFromMemory failed: {}", uri);
                rec->state.store(AssetState::eFailed, std::memory_order_release);
                return AssetHandle<vasset::VAnimation, resource::CpuAsset>(rec);
            }

            rec->cpu = std::move(cpu);
            rec->gpuIndex.store(0u, std::memory_order_release);
            rec->state.store(AssetState::eReady, std::memory_order_release);
        }

        return AssetHandle<vasset::VAnimation, resource::CpuAsset>(rec);
    }

    AssetHandle<vasset::VTexture, resource::GpuTexture> AssetSystem::loadTextureSync(const CoreUUID& uuid)
    {
        auto* rec = m_TextureCache.findOrCreate(uuid);
        if (!rec)
            return {};
        markAssetUsed(*rec, m_LastUpdateFrame);

        if (rec->state.load(std::memory_order_acquire) == AssetState::eReady &&
            rec->gpuIndex.load(std::memory_order_acquire) != std::numeric_limits<uint32_t>::max())
        {
            return AssetHandle<vasset::VTexture, resource::GpuTexture>(rec);
        }

        if (rec->state.load(std::memory_order_acquire) == AssetState::eLoadingCPU)
            waitForCpuLoadTasks();

        // CPU stage (sync baseline)
        if (rec->state.load(std::memory_order_acquire) == AssetState::eUnloaded)
        {
            rec->state.store(AssetState::eLoadingCPU, std::memory_order_release);

            std::string uri;
            if (!resolveUUIDToUri(uuid, uri))
            {
                VULTRA_CLIENT_ERROR("loadTextureSync: cannot resolve uuid {}", uuid.toString());
                rec->state.store(AssetState::eFailed, std::memory_order_release);
                return AssetHandle<vasset::VTexture, resource::GpuTexture>(rec);
            }

            auto br = readTextureAssetBytes(uri);
            if (!br)
            {
                VULTRA_CLIENT_ERROR("loadTextureSync: failed to read {}", uri);
                rec->state.store(AssetState::eFailed, std::memory_order_release);
                return AssetHandle<vasset::VTexture, resource::GpuTexture>(rec);
            }

            auto cpu = isBuiltinTextureUri(uri) ? makeTextureFromBytes(uri, br.value()) :
                                                  std::make_unique<vasset::VTexture>();
            if (!cpu || (!isBuiltinTextureUri(uri) && !vasset::loadTextureFromMemory(br.value(), *cpu)))
            {
                VULTRA_CLIENT_ERROR("loadTextureSync: vasset::loadTextureFromMemory failed: {}", uri);
                rec->state.store(AssetState::eFailed, std::memory_order_release);
                return AssetHandle<vasset::VTexture, resource::GpuTexture>(rec);
            }

            rec->cpu = std::move(cpu);
            rec->state.store(AssetState::eCPUReady, std::memory_order_release);
        }

        // Enqueue GPU upload
        if (rec->state.load(std::memory_order_acquire) == AssetState::eCPUReady)
        {
            bool expected = false;
            if (rec->uploadQueued.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            {
                rec->state.store(AssetState::eUploadQueued, std::memory_order_release);

                std::scoped_lock lock(m_UploadQueueMutex);
                m_UploadQueue.push_back(UploadCmd {UploadCmd::Kind::eTexture, uuid});
            }
        }

        update(/*frameIndex*/ 0);

        return AssetHandle<vasset::VTexture, resource::GpuTexture>(rec);
    }

    AssetHandle<vasset::VTexture, resource::GpuTexture> AssetSystem::loadTextureSync(std::string_view uri)
    {
        CoreUUID uuid {};
        if (!resolveUriToUUID(uri, uuid))
            return {};
        return loadTextureSync(uuid);
    }

    AssetHandle<vasset::VTexture, resource::GpuTexture> AssetSystem::loadTextureAsync(std::string_view uri)
    {
        CoreUUID uuid {};
        if (!resolveUriToUUID(uri, uuid))
            return {};
        return loadTextureAsync(uuid);
    }

    AssetHandle<vasset::VSkeleton, resource::CpuAsset> AssetSystem::loadSkeletonSync(std::string_view uri)
    {
        CoreUUID uuid {};
        if (!resolveUriToUUID(uri, uuid))
            return {};
        return loadSkeletonSync(uuid);
    }

    AssetHandle<vasset::VSkeleton, resource::CpuAsset> AssetSystem::loadSkeletonAsync(std::string_view uri)
    {
        CoreUUID uuid {};
        if (!resolveUriToUUID(uri, uuid))
            return {};
        return loadSkeletonAsync(uuid);
    }

    AssetHandle<vasset::VAnimation, resource::CpuAsset> AssetSystem::loadAnimationSync(std::string_view uri)
    {
        CoreUUID uuid {};
        if (!resolveUriToUUID(uri, uuid))
            return {};
        return loadAnimationSync(uuid);
    }

    AssetHandle<vasset::VAnimation, resource::CpuAsset> AssetSystem::loadAnimationAsync(std::string_view uri)
    {
        CoreUUID uuid {};
        if (!resolveUriToUUID(uri, uuid))
            return {};
        return loadAnimationAsync(uuid);
    }

    AssetHandle<vasset::VMesh, resource::GpuMesh> AssetSystem::loadMeshSync(const CoreUUID& uuid)
    {
        auto* rec = m_MeshCache.findOrCreate(uuid);
        if (!rec)
            return {};
        markAssetUsed(*rec, m_LastUpdateFrame);

        // Already resident on GPU
        if (rec->state.load(std::memory_order_acquire) == AssetState::eReady &&
            rec->gpuIndex.load(std::memory_order_acquire) != std::numeric_limits<uint32_t>::max())
        {
            return AssetHandle<vasset::VMesh, resource::GpuMesh>(rec);
        }

        if (rec->state.load(std::memory_order_acquire) == AssetState::eLoadingCPU)
            waitForCpuLoadTasks();

        // CPU stage (sync baseline)
        if (rec->state.load(std::memory_order_acquire) == AssetState::eUnloaded)
        {
            rec->state.store(AssetState::eLoadingCPU, std::memory_order_release);

            std::string uri;
            if (!resolveUUIDToUri(uuid, uri))
            {
                VULTRA_CLIENT_ERROR("loadMeshSync: cannot resolve uuid {}", uuid.toString());
                rec->state.store(AssetState::eFailed, std::memory_order_release);
                return AssetHandle<vasset::VMesh, resource::GpuMesh>(rec);
            }

            auto br = m_VFS.readAll(uri);
            if (!br)
            {
                VULTRA_CLIENT_ERROR("loadMeshSync: failed to read {}", uri);
                rec->state.store(AssetState::eFailed, std::memory_order_release);
                return AssetHandle<vasset::VMesh, resource::GpuMesh>(rec);
            }

            auto cpu = std::make_unique<vasset::VMesh>();
            auto r   = vasset::loadMeshFromMemory(br.value(), *cpu);
            if (!r)
            {
                VULTRA_CLIENT_ERROR("loadMeshSync: vasset::loadMeshFromMemory failed: {}", uri);
                rec->state.store(AssetState::eFailed, std::memory_order_release);
                return AssetHandle<vasset::VMesh, resource::GpuMesh>(rec);
            }

            // Store CPU copy (needed for deferred GPU upload).
            rec->cpu = std::move(cpu);
            rec->state.store(AssetState::eCPUReady, std::memory_order_release);
        }

        // Enqueue GPU upload (sync bring-up still goes through the queue so we can migrate to async later).
        if (rec->state.load(std::memory_order_acquire) == AssetState::eCPUReady)
        {
            bool expected = false;
            if (rec->uploadQueued.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            {
                rec->state.store(AssetState::eUploadQueued, std::memory_order_release);

                std::scoped_lock lock(m_UploadQueueMutex);
                m_UploadQueue.push_back(UploadCmd {UploadCmd::Kind::eMesh, uuid});
            }
        }

        // Sync baseline: execute uploads immediately. In async mode, the engine main loop calls update() once per
        // frame.
        update(/*frameIndex*/ 0);

        return AssetHandle<vasset::VMesh, resource::GpuMesh>(rec);
    }

    AssetHandle<vasset::VMesh, resource::GpuMesh> AssetSystem::loadMeshSync(std::string_view uri)
    {
        CoreUUID uuid {};
        if (!resolveUriToUUID(uri, uuid))
            return {};
        return loadMeshSync(uuid);
    }

    AssetHandle<vasset::VMesh, resource::GpuMesh> AssetSystem::loadMeshAsync(std::string_view uri)
    {
        CoreUUID uuid {};
        if (!resolveUriToUUID(uri, uuid))
            return {};
        return loadMeshAsync(uuid);
    }

    AssetHandle<vasset::VGaussianSplat, resource::GpuGaussianSplat>
    AssetSystem::loadGaussianSplatSync(const CoreUUID& uuid)
    {
        auto* rec = m_GaussianSplatCache.findOrCreate(uuid);
        if (!rec)
            return {};
        markAssetUsed(*rec, m_LastUpdateFrame);

        if (rec->state.load(std::memory_order_acquire) == AssetState::eReady &&
            rec->gpuIndex.load(std::memory_order_acquire) != std::numeric_limits<uint32_t>::max())
        {
            return AssetHandle<vasset::VGaussianSplat, resource::GpuGaussianSplat>(rec);
        }

        if (rec->state.load(std::memory_order_acquire) == AssetState::eLoadingCPU)
            waitForCpuLoadTasks();

        if (rec->state.load(std::memory_order_acquire) == AssetState::eUnloaded)
        {
            rec->state.store(AssetState::eLoadingCPU, std::memory_order_release);

            std::string uri;
            if (!resolveUUIDToUri(uuid, uri))
            {
                VULTRA_CLIENT_ERROR("loadGaussianSplatSync: cannot resolve uuid {}", uuid.toString());
                rec->state.store(AssetState::eFailed, std::memory_order_release);
                return AssetHandle<vasset::VGaussianSplat, resource::GpuGaussianSplat>(rec);
            }

            auto br = m_VFS.readAll(uri);
            if (!br)
            {
                VULTRA_CLIENT_ERROR("loadGaussianSplatSync: failed to read {}", uri);
                rec->state.store(AssetState::eFailed, std::memory_order_release);
                return AssetHandle<vasset::VGaussianSplat, resource::GpuGaussianSplat>(rec);
            }

            auto cpu = std::make_unique<vasset::VGaussianSplat>();
            auto r   = vasset::loadGaussianSplatFromMemory(br.value(), *cpu);
            if (!r)
            {
                VULTRA_CLIENT_ERROR("loadGaussianSplatSync: vasset::loadGaussianSplatFromMemory failed: {}", uri);
                rec->state.store(AssetState::eFailed, std::memory_order_release);
                return AssetHandle<vasset::VGaussianSplat, resource::GpuGaussianSplat>(rec);
            }

            rec->cpu = std::move(cpu);
            rec->state.store(AssetState::eCPUReady, std::memory_order_release);
        }

        if (rec->state.load(std::memory_order_acquire) == AssetState::eCPUReady)
        {
            bool expected = false;
            if (rec->uploadQueued.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            {
                rec->state.store(AssetState::eUploadQueued, std::memory_order_release);

                std::scoped_lock lock(m_UploadQueueMutex);
                m_UploadQueue.push_back(UploadCmd {UploadCmd::Kind::eGaussianSplat, uuid});
            }
        }

        update(/*frameIndex*/ 0);

        return AssetHandle<vasset::VGaussianSplat, resource::GpuGaussianSplat>(rec);
    }

    AssetHandle<vasset::VGaussianSplat, resource::GpuGaussianSplat>
    AssetSystem::loadGaussianSplatSync(std::string_view uri)
    {
        CoreUUID uuid {};
        if (!resolveUriToUUID(uri, uuid))
            return {};
        return loadGaussianSplatSync(uuid);
    }

    AssetHandle<vasset::VGaussianSplat, resource::GpuGaussianSplat>
    AssetSystem::loadGaussianSplatAsync(std::string_view uri)
    {
        CoreUUID uuid {};
        if (!resolveUriToUUID(uri, uuid))
            return {};
        return loadGaussianSplatAsync(uuid);
    }

    std::string AssetSystem::resolveUri(const std::string_view uri) const
    {
        auto vbaseUri = vfilesystem::parse_uri(uri);
        return m_Desc.assetRoot + vbaseUri.path.str().data();
    }

    bool AssetSystem::resolveAssetUri(const CoreUUID& uuid, std::string& outUri) const
    {
        return resolveUUIDToUri(uuid, outUri);
    }

    bool AssetSystem::reimportAsset(std::string_view uri, const bool forceReimport)
    {
#ifdef VULTRA_HAS_VASSET_IMPORT
        if (ctx().config.asset.loadFromVPK)
            return false;

        const auto             physicalPath = std::filesystem::path(resolveUri(uri)).lexically_normal();
        vasset::VAssetImporter importer {m_Registry};
        m_LastImportDiagnostics.clear();
        importer.setOptions(makeAssetImportOptions(true, &m_LastImportDiagnostics));
        auto result = importer.importOrReimportAsset(physicalPath.generic_string(), forceReimport);
        if (!result)
        {
            VULTRA_CORE_ERROR("[AssetSystem] Failed to reimport asset '{}'", uri);
            return false;
        }

        std::error_code ec;
        const auto sourceRelativePath = std::filesystem::relative(physicalPath, m_Desc.assetRoot, ec);
        if (!ec && !sourceRelativePath.empty())
        {
            const auto relativeText = sourceRelativePath.generic_string();
            if (!relativeText.starts_with("../"))
                emitImportedMaterialAssets(relativeText);
        }

        const auto registryPath =
            (std::filesystem::path(m_Desc.assetRoot) / m_Desc.importedFolder / m_Desc.registryFile).generic_string();
        m_Registry.save(registryPath);
        m_Resolver.loadFromAssetRegistry(m_Registry);
        m_Resolver.setScheme(m_Desc.scheme);
        return true;
#else
        static_cast<void>(uri);
        static_cast<void>(forceReimport);
        return false;
#endif
    }

    bool AssetSystem::reloadRegistry()
    {
        if (ctx().config.asset.loadFromVPK)
            return false;

        const auto registryPath =
            (std::filesystem::path(m_Desc.assetRoot) / m_Desc.importedFolder / m_Desc.registryFile).generic_string();
        vasset::VAssetRegistry registry;
        registry.setAssetRootPath(m_Desc.assetRoot);
        registry.setImportedFolderName(m_Desc.importedFolder);
        if (!registry.load(registryPath))
        {
            VULTRA_CORE_WARN("[AssetSystem] Failed to reload asset registry: {}", registryPath);
            return false;
        }

        const auto beforeCleanup = registry.getRegistry().size();
        registry.cleanup();
        if (registry.getRegistry().size() < beforeCleanup && !registry.save(registryPath))
            VULTRA_CORE_WARN("[AssetSystem] Failed to save cleaned asset registry: {}", registryPath);

        m_Registry = std::move(registry);
        m_Resolver.loadFromAssetRegistry(m_Registry);
        m_Resolver.setScheme(m_Desc.scheme);
        VULTRA_CORE_INFO("[AssetSystem] Reloaded asset registry. Registry entries: {}", m_Registry.getRegistry().size());
        return true;
    }

    vbase::Result<std::string, std::string> AssetSystem::loadTextAssetSync(std::string_view uri)
    {
        {
            std::scoped_lock lock(m_TextOverrideMutex);
            if (auto it = m_TextAssetOverrides.find(std::string(uri)); it != m_TextAssetOverrides.end())
                return vbase::Result<std::string, std::string>::ok(it->second);
        }

        if (uri.starts_with("builtin://"))
        {
            if (const auto text = builtinRenderGraphText(uri); !text.empty())
                return vbase::Result<std::string, std::string>::ok(std::string(text));
            if (isBuiltinMaterialUri(uri))
            {
                if (auto text = readBuiltinTextFile(builtinMaterialPathForUri(uri)))
                    return text;
            }
        }

        if (!ctx().config.asset.loadFromVPK)
        {
            const auto      sourcePath = std::filesystem::path(resolveUri(uri)).lexically_normal();
            std::error_code ec;
            if (shouldReadPhysicalTextSourceDirectly(sourcePath) && std::filesystem::is_regular_file(sourcePath, ec))
            {
                std::ifstream file(sourcePath, std::ios::binary | std::ios::ate);
                if (file)
                {
                    const auto  size = static_cast<std::streamsize>(file.tellg());
                    std::string text(static_cast<size_t>(std::max<std::streamsize>(size, 0)), '\0');
                    file.seekg(0);
                    if (text.empty() || file.read(text.data(), size))
                        return vbase::Result<std::string, std::string>::ok(std::move(text));
                }
            }
        }

        auto bytesResult = m_VFS.readAll(uri);
        if (!bytesResult)
            return vbase::Result<std::string, std::string>::err("Failed to read text asset: " + std::string(uri));

        const auto& bytes = bytesResult.value();
        return vbase::Result<std::string, std::string>::ok(
            std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    }

    void AssetSystem::setTextAssetOverride(std::string_view uri, std::string text)
    {
        std::scoped_lock lock(m_TextOverrideMutex);
        m_TextAssetOverrides[std::string(uri)] = std::move(text);
        if (m_GpuResourceService)
            m_GpuResourceService->markContentDirty();
    }

    void AssetSystem::clearTextAssetOverride(std::string_view uri)
    {
        std::scoped_lock lock(m_TextOverrideMutex);
        m_TextAssetOverrides.erase(std::string(uri));
        if (m_GpuResourceService)
            m_GpuResourceService->markContentDirty();
    }

    vbase::Result<std::vector<uint8_t>, std::string> AssetSystem::loadBinaryAssetSync(std::string_view uri)
    {
        auto bytesResult = m_VFS.readAll(uri);
        if (!bytesResult)
            return vbase::Result<std::vector<uint8_t>, std::string>::err("Failed to read binary asset: " +
                                                                         std::string(uri));

        const auto&          bytes = bytesResult.value();
        std::vector<uint8_t> out;
        out.reserve(bytes.size());
        for (const auto byte : bytes)
            out.push_back(static_cast<uint8_t>(byte));

        return vbase::Result<std::vector<uint8_t>, std::string>::ok(std::move(out));
    }
} // namespace vultra
