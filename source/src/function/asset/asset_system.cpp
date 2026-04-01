#include "vultra/function/asset/asset_system.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/structs/vertex_attributes.hpp"
#include "vultra/function/resource/vtexture_loader.hpp"
#include "vultra/function/services/render_backend_service.hpp"

#ifdef VULTRA_HAS_VASSET_IMPORT
#include <vasset/editor_filesystem.hpp>
#include <vasset/vasset_importers.hpp>
#endif
#include <vasset/vgaussiansplat.hpp>
#include <vasset/vmaterial.hpp>

#include <vfilesystem/backends/physical_filesystem.hpp>

#include <glm/gtc/packing.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace vultra
{
    namespace
    {
        // Very small fallback: pack a subset of material params into a fixed block.
        // This is intentionally simple; later, vshadersystem reflection will pack arbitrary params.
        struct alignas(16) MaterialParamsPBRMR
        {
            glm::vec4 baseColor {1, 1, 1, 1};
            float     metallicFactor {1.0f};
            float     roughnessFactor {1.0f};
            uint32_t  baseColorTex {0};
            uint32_t  normalTex {0};
            uint32_t  mrTex {0};
            uint32_t  occlusionTex {0};
            uint32_t  emissiveTex {0};
            uint32_t  pad0 {0};
            uint32_t  pad1 {0};
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
                add(6, VertexAttribute::Type::eFloat4);

            if (flags & VVertexFlags::eJointWeights)
                add(7, VertexAttribute::Type::eFloat4);

            stride = offset;

            return attrs;
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
                            memcpy(ptr, &mesh.jointIndices[i], sizeof(glm::vec4));
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

        glm::quat sanitizeAndNormalizeQuat(const glm::vec4& xyzw)
        {
            if (!std::isfinite(xyzw.x) || !std::isfinite(xyzw.y) || !std::isfinite(xyzw.z) || !std::isfinite(xyzw.w))
                return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

            glm::quat   q(xyzw.w, xyzw.x, xyzw.y, xyzw.z);
            const float len2 = glm::dot(q, q);
            if (!(len2 > 1e-12f))
                return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

            return glm::normalize(q);
        }
    } // namespace

    bool AssetSystem::onInit()
    {
        VULTRA_CORE_INFO("[AssetSystem] Initializing...");

        VULTRA_CORE_TRACE("[AssetSystem] Getting render backend");
        auto& backend  = ctx().services.require<IRenderBackendService>();
        m_RenderDevice = &backend.renderDevice();

        VULTRA_CORE_TRACE("[AssetSystem] Getting GPU resource service");
        m_GpuResourceService = &ctx().services.require<IGpuResourceService>();

        // Default config (can be overridden at runtime/editor).
        configure(AssetSystemDesc {});

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

        {
            std::scoped_lock lock(m_UploadQueueMutex);
            m_UploadQueue.clear();
        }
        m_MeshCache.clear();
        m_TextureCache.clear();
        m_GaussianSplatCache.clear();
        m_TexUUIDToBindlessIndex.clear();
        m_RenderDevice       = nullptr;
        m_GpuResourceService = nullptr;
    }

    void AssetSystem::configure(const AssetSystemDesc& desc)
    {
        m_Desc                = desc;
        m_Desc.assetRoot      = ctx().config.asset.assetRoot;
        m_Desc.importedFolder = ctx().config.asset.importedFolder;
        m_Desc.registryFile   = ctx().config.asset.registryFile;
        m_Desc.vpkFile        = ctx().config.asset.vpkFile;

        const auto resolveAssetPath = [&](const std::string& p) {
            std::filesystem::path path {p};
            if (path.is_relative())
                path = std::filesystem::path(m_Desc.assetRoot) / path;
            return path;
        };

        m_Registry.setAssetRootPath(m_Desc.assetRoot);
        m_Registry.setImportedFolderName(m_Desc.importedFolder);

        if (ctx().config.asset.loadFromVPK)
        {
            // For production, mount the VPK file (read-only). The VPK's embedded registry is the source of truth
            // for UUID -> source path mapping.
            const auto vpkPath       = resolveAssetPath(m_Desc.vpkFile).generic_string();
            auto       vpkFileSystem = createRef<vasset::VpkFileSystem>(vpkPath);
            auto       openResult    = vpkFileSystem->openPackage();
            if (!openResult)
            {
                VULTRA_CORE_ERROR("[AssetSystem] Failed to open VPK file: {}", vpkPath);
                return;
            }

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
                else if (ext == ".vmanifest")
                    inferredType = vasset::VAssetType::eSceneManifest;
                else if (ext == ".lua")
                    inferredType = vasset::VAssetType::eScriptLua;
                else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" ||
                         ext == ".gif" || ext == ".psd" || ext == ".pic" || ext == ".hdr" || ext == ".ktx" ||
                         ext == ".dds" || ext == ".ktx2")
                    inferredType = vasset::VAssetType::eTexture;
                else if (ext == ".fbx" || ext == ".obj" || ext == ".gltf" || ext == ".dae")
                    inferredType = vasset::VAssetType::eMesh;
                else if (ext == ".ply" || ext == ".spz" || ext == ".splat" || ext == ".ksplat")
                    inferredType = vasset::VAssetType::eGaussianSplat;

                m_Registry.registerAsset(entry.uuid, logicalPath, logicalPath, inferredType);
            }

            m_Resolver.loadFromVPK(vpk);
            m_VFS.mount(vpkFileSystem, desc.scheme);
        }
        else
        {
            // For development, mount the editor remap filesystem, which allows transparent access to source assets and
            // imported assets. Runtime-only builds mount the physical filesystem directly and expect pre-baked assets.
            auto registryPath = (std::filesystem::path(m_Desc.assetRoot) / m_Desc.importedFolder / m_Desc.registryFile)
                                    .generic_string();
            if (!std::filesystem::exists(registryPath) || !m_Registry.load(registryPath))
            {
                VULTRA_CORE_WARN("[AssetSystem] Failed to load asset registry from file: {}", registryPath);
#ifdef VULTRA_HAS_VASSET_IMPORT
                vasset::VAssetImporter importer {m_Registry};
                importer.importOrReimportAssetFolder(desc.assetRoot);
                m_Registry.save(registryPath);
#else
                VULTRA_CORE_WARN("[AssetSystem] Runtime-only vasset build cannot auto-import source assets. "
                                 "Expect pre-baked assets or an existing registry.");
#endif
            }
            else
            {
                VULTRA_CORE_INFO("[AssetSystem] Loaded asset registry from file: {}", registryPath);
            }

            m_Resolver.loadFromAssetRegistry(m_Registry);

#ifdef VULTRA_HAS_VASSET_IMPORT
            m_VFS.mount(createRef<vasset::EditorRemapFileSystem>(
                            createRef<vfilesystem::PhysicalFileSystem>(vfilesystem::Path {desc.assetRoot})),
                        desc.scheme);
#else
            m_VFS.mount(createRef<vfilesystem::PhysicalFileSystem>(vfilesystem::Path {desc.assetRoot}), desc.scheme);
#endif
        }

        m_Resolver.setScheme(desc.scheme);

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

        // Drain upload commands
        std::vector<UploadCmd> cmds;
        {
            std::scoped_lock lock(m_UploadQueueMutex);
            cmds.swap(m_UploadQueue);
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

    bool AssetSystem::resolveUUIDToUri(const CoreUUID& uuid, std::string& outUri) const
    {
        auto entry = m_Registry.lookup(uuid);
        if (entry.type == vasset::VAssetType::eUnknown)
            return false;

        // Prefer the source path so runtime can read from VPK source-URI mounts or from the physical source tree.
        // Fall back to importedPath only if an entry is incomplete.
        const std::string& path = !entry.sourcePath.empty() ? entry.sourcePath : entry.importedPath;
        outUri                  = m_Desc.scheme + "://" + path;
        return true;
    }

    bool AssetSystem::resolveUriToUUID(std::string_view uri, CoreUUID& outUUID) const
    {
        return m_Resolver.reverseResolve(uri, outUUID);
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
                p.baseColorTex    = resolveBindlessTextureIndex(CoreUUID(m.core.pbrMR.baseColorTexture.uuid));
                p.normalTex       = resolveBindlessTextureIndex(CoreUUID(m.core.pbrMR.normalTexture.uuid));
                p.mrTex           = resolveBindlessTextureIndex(CoreUUID(m.core.pbrMR.metallicRoughnessTexture.uuid));
                p.occlusionTex    = resolveBindlessTextureIndex(CoreUUID(m.core.pbrMR.ambientOcclusionTexture.uuid));
                p.emissiveTex     = resolveBindlessTextureIndex(CoreUUID(m.core.pbrMR.emissiveTexture.uuid));
                blockOffset       = allocBlock(&p, sizeof(p));
                break;
            }
            case vasset::VMaterialModel::ePBRSpecularGlossiness: {
                gm.model = GpuMaterialModel::ePBRSpecularGlossiness;
                MaterialParamsPBRSG p;
                p.diffuseColor     = m.core.pbrSG.diffuseColor;
                p.specularFactor   = m.core.pbrSG.specularFactor;
                p.glossinessFactor = m.core.pbrSG.glossinessFactor;
                p.diffuseColorTex  = resolveBindlessTextureIndex(CoreUUID(m.core.pbrSG.diffuseTexture.uuid));
                p.specularGlossinessTex =
                    resolveBindlessTextureIndex(CoreUUID(m.core.pbrSG.specularGlossinessTexture.uuid));
                blockOffset = allocBlock(&p, sizeof(p));
                break;
            }
            case vasset::VMaterialModel::eUnlit: {
                gm.model = GpuMaterialModel::eUnlit;
                MaterialParamsUnlit p;
                p.color     = m.core.unlit.color;
                p.colorTex  = resolveBindlessTextureIndex(CoreUUID(m.core.unlit.colorTexture.uuid));
                blockOffset = allocBlock(&p, sizeof(p));
                break;
            }
            case vasset::VMaterialModel::ePhong:
            default: {
                gm.model = GpuMaterialModel::ePhong;
                MaterialParamsPhong p;
                p.diffuse           = m.core.phong.diffuse;
                p.specularShininess = glm::vec4(m.core.phong.specular, m.core.phong.shininess);
                p.diffuseTex        = resolveBindlessTextureIndex(CoreUUID(m.core.phong.diffuseTexture.uuid));
                blockOffset         = allocBlock(&p, sizeof(p));
                break;
            }
        }

        gm.blockOffsetBytes = blockOffset;
        gm.tableIndex       = static_cast<uint32_t>(pool.materials.size());
        pool.materials.push_back(gm);
        pool.materialTableDirty = true;
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
        pool.meshes[meshIndex].subMeshes.reserve(cpuMesh.subMeshes.size());
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
            const glm::quat q = sanitizeAndNormalizeQuat(p.rotation);
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
        return index;
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

            auto br = m_VFS.readAll(uri);
            if (!br)
            {
                VULTRA_CLIENT_ERROR("loadTextureSync: failed to read {}", uri);
                rec->state.store(AssetState::eFailed, std::memory_order_release);
                return AssetHandle<vasset::VTexture, resource::GpuTexture>(rec);
            }

            auto cpu = std::make_unique<vasset::VTexture>();
            auto r   = vasset::loadTextureFromMemory(br.value(), *cpu);
            if (!r)
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

    std::string AssetSystem::resolveUri(const std::string_view uri) const
    {
        auto vbaseUri = vfilesystem::parse_uri(uri);
        return m_Desc.assetRoot + vbaseUri.path.str().data();
    }

    vbase::Result<std::string, std::string> AssetSystem::loadTextAssetSync(std::string_view uri)
    {
        auto bytesResult = m_VFS.readAll(uri);
        if (!bytesResult)
            return vbase::Result<std::string, std::string>::err("Failed to read text asset: " + std::string(uri));

        const auto& bytes = bytesResult.value();
        return vbase::Result<std::string, std::string>::ok(
            std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    }
} // namespace vultra

