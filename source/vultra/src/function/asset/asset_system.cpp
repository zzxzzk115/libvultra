#include "vultra/function/asset/asset_system.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/structs/render_mesh.hpp"
#include "vultra/core/rhi/structs/vertex_attributes.hpp"
#include "vultra/function/asset/builtin_assets.hpp"
#include "vultra/function/asset/builtin_resource_ids.hpp"
#include "vultra/function/resource/vtexture_loader.hpp"
#include "vultra/function/rendering/srp/builtin/builtin_rendergraph_registry.hpp"
#include "vultra/function/services/render_backend_service.hpp"

#ifdef VULTRA_HAS_VASSET_IMPORT
#include <builtin_shaders.hpp>
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

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>
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

        bool isBuiltinCitrusOrchardSkyTextureUri(std::string_view uri)
        {
            return uri == kBuiltinCitrusOrchardSkyTextureUri;
        }

        bool isBuiltinTextureUri(std::string_view uri)
        {
            return uri.starts_with(kBuiltinTextureUriPrefix);
        }

        std::filesystem::path builtinTexturePathForUri(std::string_view uri)
        {
            if (!isBuiltinTextureUri(uri))
                return {};
            const auto rel = std::string(uri.substr(kBuiltinTextureUriPrefix.size()));
            return (std::filesystem::path("builtin") / "textures" / std::filesystem::path(rel)).lexically_normal();
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

        // Very small fallback: pack a subset of material params into a fixed block.
        // This is intentionally simple; later, vshadersystem reflection will pack arbitrary params.
        struct alignas(16) MaterialParamsPBRMR
        {
            glm::vec4 baseColor {1, 1, 1, 1};
            float     metallicFactor {1.0f};
            float     roughnessFactor {1.0f};
            float     alphaCutoff {0.5f};
            uint32_t  alphaMode {0};
            uint32_t  baseColorTex {0};
            uint32_t  normalTex {0};
            uint32_t  mrTex {0};
            uint32_t  metallicTex {0};
            uint32_t  roughnessTex {0};
            uint32_t  occlusionTex {0};
            uint32_t  emissiveTex {0};
            uint32_t  doubleSided {0};
            uint32_t  mrTextureMode {0};
            uint32_t  pad1 {0};
            uint32_t  pad2 {0};
        };
        static_assert(sizeof(MaterialParamsPBRMR) % 16 == 0);

        struct alignas(16) MaterialParamsPBRSG
        {
            glm::vec4 diffuseColor {1, 1, 1, 1};
            glm::vec3 specularFactor {1, 1, 1};
            float     glossinessFactor {1.0f};
            uint32_t  diffuseColorTex {0};
            uint32_t  specularGlossinessTex {0};
            uint32_t  pad0 {0};
            uint32_t  pad1 {0};
        };
        static_assert(sizeof(MaterialParamsPBRSG) % 16 == 0);

        struct alignas(16) MaterialParamsUnlit
        {
            glm::vec4 color {1, 1, 1, 1};
            uint32_t  colorTex {0};
            uint32_t  pad0 {0};
            uint32_t  pad1 {0};
            uint32_t  pad2 {0};
        };
        static_assert(sizeof(MaterialParamsUnlit) % 16 == 0);

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
            options.shaderVirtualIncludes.reserve(builtin_shader_include_sources_count);
            for (size_t i = 0; i < builtin_shader_include_sources_count; ++i)
            {
                const auto& source = builtin_shader_include_sources[i];
                options.shaderVirtualIncludes.push_back({
                    .virtualPath = source.path,
                    .sourceText  = std::string(reinterpret_cast<const char*>(source.data), source.size),
                });
            }
            return options;
        }
#endif

        struct alignas(16) MaterialParamsPhong
        {
            glm::vec4 diffuse {1, 1, 1, 1};
            glm::vec4 specularShininess {1, 1, 1, 32}; // xyz = specular, w = shininess
            uint32_t  diffuseTex {0};
            uint32_t  pad0 {0};
            uint32_t  pad1 {0};
            uint32_t  pad2 {0};
        };
        static_assert(sizeof(MaterialParamsPhong) % 16 == 0);

        constexpr std::size_t kMaxUploadCommandsPerFrame        = 2;
        constexpr std::size_t kMaxMaterialRefreshChecksPerFrame = 8;

        using namespace resource;
        using namespace rhi;
        using namespace vasset;

        rhi::VertexAttributes buildVertexAttributes(VVertexFlags flags, uint32_t& stride)
        {
            VertexAttributes attrs;

            uint32_t offset = 0;

            auto add = [&](uint32_t loc, VertexAttribute::Type type) {
                attrs[loc] = VertexAttribute {loc, type, offset};

                offset += getSize(type);
            };

            if (flags & VVertexFlags::ePosition)
                add(0, VertexAttribute::Type::eFloat3);

            if (flags & VVertexFlags::eNormal)
                add(1, VertexAttribute::Type::eFloat3);

            if (flags & VVertexFlags::eColor)
                add(2, VertexAttribute::Type::eFloat3);

            if (flags & VVertexFlags::eTexCoord0)
                add(3, VertexAttribute::Type::eFloat2);

            if (flags & VVertexFlags::eTexCoord1)
                add(4, VertexAttribute::Type::eFloat2);

            if (flags & VVertexFlags::eTangent)
                add(5, VertexAttribute::Type::eFloat4);

            if (flags & VVertexFlags::eJointIndices)
                add(6, VertexAttribute::Type::eInt4);

            if (flags & VVertexFlags::eJointWeights)
                add(7, VertexAttribute::Type::eFloat4);

            stride = offset;

            return attrs;
        }

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

        struct PackedVertexLayout
        {
            uint32_t              stride {0};
            rhi::VertexAttributes attributes;
        };

        std::vector<uint8_t> packVertices(const VMesh& mesh, const PackedVertexLayout& layout)
        {
            const auto&    attrs  = layout.attributes;
            const uint32_t stride = layout.stride;

            std::vector<uint8_t> buffer;

            buffer.resize(mesh.vertexCount * stride);

            for (uint32_t i = 0; i < mesh.vertexCount; i++)
            {
                uint8_t* dst = buffer.data() + i * stride;

                for (const auto& [location, attr] : attrs)
                {
                    uint8_t* ptr = dst + attr.offset;

                    switch (location)
                    {
                        case 0:
                            memcpy(ptr, &mesh.positions[i], sizeof(glm::vec3));
                            break;

                        case 1:
                            memcpy(ptr, &mesh.normals[i], sizeof(glm::vec3));
                            break;

                        case 2:
                            memcpy(ptr, &mesh.colors[i], sizeof(glm::vec3));
                            break;

                        case 3:
                            memcpy(ptr, &mesh.texCoords0[i], sizeof(glm::vec2));
                            break;

                        case 4:
                            memcpy(ptr, &mesh.texCoords1[i], sizeof(glm::vec2));
                            break;

                        case 5:
                            memcpy(ptr, &mesh.tangents[i], sizeof(glm::vec4));
                            break;

                        case 6:
                            memcpy(ptr, &mesh.jointIndices[i], sizeof(glm::ivec4));
                            break;

                        case 7:
                            memcpy(ptr, &mesh.jointWeights[i], sizeof(glm::vec4));
                            break;
                    }
                }
            }

            return buffer;
        }

        float sigmoid(float x) { return 1.0f / (1.0f + std::exp(-x)); }

        [[nodiscard]] uint64_t stringBytes(const std::string& value) { return static_cast<uint64_t>(value.capacity()); }

        template<typename T>
        [[nodiscard]] uint64_t vectorBytes(const std::vector<T>& value)
        {
            return static_cast<uint64_t>(value.capacity()) * sizeof(T);
        }

        [[nodiscard]] uint64_t estimateVMaterialPropertyBytes(const vasset::VMaterialProperty& property)
        {
            return sizeof(property) + stringBytes(property.key) + vectorBytes(property.data);
        }

        [[nodiscard]] uint64_t estimateVMaterialBytes(const vasset::VMaterial& material)
        {
            uint64_t bytes = sizeof(material) + stringBytes(material.name) + vectorBytes(material.textures);
            for (const auto& property : material.properties)
            {
                bytes += estimateVMaterialPropertyBytes(property);
            }
            return bytes;
        }

        [[nodiscard]] uint64_t estimateVSubMeshBytes(const vasset::VSubMesh& subMesh)
        {
            return sizeof(subMesh) + stringBytes(subMesh.name) + vectorBytes(subMesh.meshletGroup.meshlets) +
                   vectorBytes(subMesh.meshletGroup.meshletVertices) +
                   vectorBytes(subMesh.meshletGroup.meshletTriangles);
        }

        [[nodiscard]] uint64_t estimateVMeshBytes(const vasset::VMesh& mesh)
        {
            uint64_t bytes = sizeof(mesh) + vectorBytes(mesh.positions) + vectorBytes(mesh.normals) +
                             vectorBytes(mesh.colors) + vectorBytes(mesh.texCoords0) + vectorBytes(mesh.texCoords1) +
                             vectorBytes(mesh.tangents) + vectorBytes(mesh.jointIndices) +
                             vectorBytes(mesh.jointWeights) + vectorBytes(mesh.indices) + stringBytes(mesh.name) +
                             stringBytes(mesh.sourceFileName);

            bytes += vectorBytes(mesh.subMeshes);
            for (const auto& subMesh : mesh.subMeshes)
            {
                bytes += estimateVSubMeshBytes(subMesh);
            }

            bytes += vectorBytes(mesh.materials);
            for (const auto& material : mesh.materials)
            {
                bytes += estimateVMaterialBytes(material);
            }

            return bytes;
        }

        [[nodiscard]] uint64_t estimateVTextureBytes(const vasset::VTexture& texture)
        {
            return sizeof(texture) + vectorBytes(texture.data);
        }

        [[nodiscard]] uint64_t estimateVGaussianSplatLodBytes(const vasset::VGaussianSplatLodData& lod)
        {
            return sizeof(lod) + vectorBytes(lod.importance) + vectorBytes(lod.lodLevel) + vectorBytes(lod.clusterId);
        }

        [[nodiscard]] uint64_t estimateVGaussianSplatBytes(const vasset::VGaussianSplat& splat)
        {
            return sizeof(splat) + vectorBytes(splat.splats) + vectorBytes(splat.sh) +
                   estimateVGaussianSplatLodBytes(splat.lod) + stringBytes(splat.name) +
                   stringBytes(splat.sourceFileName);
        }

        [[nodiscard]] uint64_t estimateVSkeletonBytes(const vasset::VSkeleton& skeleton)
        {
            uint64_t bytes = sizeof(skeleton) + vectorBytes(skeleton.jointParents) + vectorBytes(skeleton.ozzData) +
                             stringBytes(skeleton.name) + stringBytes(skeleton.sourceFileName);
            for (const auto& name : skeleton.jointNames)
                bytes += stringBytes(name);
            return bytes;
        }

        [[nodiscard]] uint64_t estimateVAnimationBytes(const vasset::VAnimation& animation)
        {
            return sizeof(animation) + vectorBytes(animation.ozzData) + stringBytes(animation.name) +
                   stringBytes(animation.sourceFileName);
        }

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
                   filename.ends_with(".vshaderlib.lua") || filename.ends_with(".vso.lua") ||
                   filename.ends_with(".vsrp.lua") || filename.ends_with(".vfeature.lua");
        }
    } // namespace

    AssetSystem::~AssetSystem() = default;

    bool AssetSystem::onInit()
    {
        VULTRA_CORE_INFO("[AssetSystem] Initializing...");

        VULTRA_CORE_TRACE("[AssetSystem] Getting render backend");
        auto& backend  = ctx().services.require<IRenderBackendService>();
        m_RenderDevice = &backend.renderDevice();

        VULTRA_CORE_TRACE("[AssetSystem] Getting GPU resource service");
        m_GpuResourceService = &ctx().services.require<IGpuResourceService>();
        m_CpuLoadScheduler   = std::make_unique<vtask::Scheduler>();

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
        // NOTE:
        // GPU upload must happen on the main/render thread. Even in sync bring-up, we keep a queue + update() shape so
        // the system can migrate to async loading later without breaking APIs.
        collectFinishedCpuLoadTasks();
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
                        for (const auto& mat : rec->cpu->materials)
                        {
                            createAndAppendGpuMaterial(mat);
                        }

                        const uint32_t meshIndex = uploadMesh(*rec->cpu, materialOffset);

                        rec->gpuIndex.store(meshIndex, std::memory_order_release);
                        rec->state.store(AssetState::eReady, std::memory_order_release);

                        // release CPU copy if not requested
                        if (!m_Desc.keepCpuCopy)
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
        if (pool.materialTableDirty)
        {
            pool.uploadMaterialTable(*m_RenderDevice);
        }

        // GC hook (TODO): Use frameIndex + refCount/lastUsedFrame to evict CPU/GPU if desired.
        (void)frameIndex;
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
            const auto path = builtinTexturePathForUri(uri);
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file)
                return readResourceBuiltinTextureBytes(uri);

            const auto          size = static_cast<std::streamsize>(file.tellg());
            std::vector<std::byte> bytes(static_cast<size_t>(std::max<std::streamsize>(size, 0)));
            file.seekg(0);
            if (!bytes.empty() && !file.read(reinterpret_cast<char*>(bytes.data()), size))
                return readResourceBuiltinTextureBytes(uri);
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
