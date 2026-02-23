#include "vultra/function/asset/asset_system.hpp"
#include "vasset/editor_filesystem.hpp"
#include "vasset/vasset_importers.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/function/services/render_backend_service.hpp"

#include <vasset/vmaterial.hpp>

#include <fstream>
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
    } // namespace

    bool AssetSystem::onInit()
    {
        ctx().services.provide<IAssetService>(this);

        auto& backend  = ctx().services.require<IRenderBackendService>();
        m_RenderDevice = &backend.renderDevice();

        // Default config (can be overridden at runtime/editor).
        configure(AssetSystemDesc {});
        return true;
    }

    void AssetSystem::onShutdown()
    {
        m_Scene.clear();
        m_TexUUIDToBindlessIndex.clear();
        m_RenderDevice = nullptr;
    }

    void AssetSystem::configure(const AssetSystemDesc& desc)
    {
        m_Desc = desc;

        if (ctx().config.loadFromVPK)
        {
            // For production, mount the VPK file (read-only).
            auto vpkFileSystem = createRef<vasset::VpkFileSystem>(desc.vpkFile);
            vpkFileSystem->openPackage();
            m_VFS.mount(vpkFileSystem, desc.scheme);
        }
        else
        {
            // Try to load existing registry from disk. This will populate the registry with previously imported assets,
            // allowing us to load them without re-importing.
            std::string registryPath = desc.registryFile;
            if (!std::filesystem::exists(registryPath) || !m_Registry.load(registryPath))
            {
                VULTRA_CORE_WARN("Failed to load asset registry from file: {}", registryPath);
                // Proceed with an empty registry, which will cause assets to be re-imported.
                m_Registry.setAssetRootPath(desc.assetRoot);
                m_Registry.setImportedFolderName(desc.importedFolder);
                vasset::VAssetImporter importer {m_Registry};
                importer.importOrReimportAssetFolder(desc.assetRoot);
                m_Registry.save(registryPath);
            }
            else
            {
                VULTRA_CORE_INFO("Loaded asset registry from file: {}", registryPath);
            }

            m_Resolver.loadFromAssetRegistry(m_Registry);

            // For development, mount the editor remap filesystem, which allows transparent access to source assets and
            // imported assets.
            m_VFS.mount(createRef<vasset::EditorRemapFileSystem>(
                            createRef<vfilesystem::PhysicalFileSystem>(vfilesystem::Path {desc.assetRoot})),
                        desc.scheme);
        }

        m_Resolver.setScheme(desc.scheme);

        // Scene owns bindless texture table. Reserve slot 0 as fallback.
        m_Scene.ensureBindlessSlot0();

        // Reset global material param pool.
        m_Scene.materialParams.reset();

        VULTRA_CLIENT_INFO("AssetSystem initialised. Registry entries: {}", m_Registry.getRegistry().size());
    }

    std::vector<std::byte> AssetSystem::readFileBytes(const std::filesystem::path& path)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f)
            return {};

        f.seekg(0, std::ios::end);
        const auto size = static_cast<size_t>(f.tellg());
        f.seekg(0, std::ios::beg);

        std::vector<std::byte> data;
        data.resize(size);
        if (size > 0)
            f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));

        return data;
    }

    bool AssetSystem::resolveUUIDToPath(const CoreUUID& uuid, std::filesystem::path& outPath) const
    {
        auto entry = m_Registry.lookup(uuid);
        if (entry.type == vasset::VAssetType::eUnknown)
            return false;

        // Prefer imported path if exists; otherwise fall back to source path.
        std::filesystem::path p = entry.importedPath.empty() ? entry.sourcePath : entry.importedPath;
        outPath                 = std::filesystem::path(m_Desc.assetRoot) / p;
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
        // NOTE: This is a minimal sync uploader.
        // We assume cpuTex contains decoded pixels in RGBA8 (or BGRA8) for now.
        // Extend later for KTX2/BCn.

        if (!m_RenderDevice || cpuTex.width == 0 || cpuTex.height == 0)
            return 0;

        // Map vasset format to rhi PixelFormat (best-effort)
        rhi::PixelFormat fmt = rhi::PixelFormat::eRGBA8_UNorm;
        // vasset::VTextureFormat exists; keep minimal mapping
        // TODO: raw loader

        auto tex = createRef<rhi::Texture>(m_RenderDevice->createTexture2D(
            {cpuTex.width, cpuTex.height}, fmt, 1, 1, rhi::ImageUsage::eSampled | rhi::ImageUsage::eTransferDst));

        // Upload via staging buffer + command buffer
        if (!cpuTex.data.empty())
        {
            auto staging = m_RenderDevice->createStagingBuffer(cpuTex.data.size(), cpuTex.data.data());
            m_RenderDevice->execute([&](rhi::CommandBuffer& cb) {
                // TODO: copy buffer to texture
            });
        }

        // Bindless ownership is in GpuScene.
        resource::GpuTexture out;
        out.texture = tex;
        return m_Scene.addTexture(std::move(out));
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

        auto allocBlock = [&](const void* src, uint32_t size) {
            return m_Scene.materialParams.allocAndUpload(*m_RenderDevice, src, size, 16);
        };

        switch (m.model)
        {
            case vasset::VMaterialModel::ePBRMetallicRoughness: {
                gm.model = GpuMaterialModel::ePBRMetallicRoughness;
                MaterialParamsPBRMR p;
                p.baseColor       = m.core.pbrMR.baseColor;
                p.metallicFactor  = m.core.pbrMR.metallicFactor;
                p.roughnessFactor = m.core.pbrMR.roughnessFactor;
                p.baseColorTex    = resolveBindlessTextureIndex(m.core.pbrMR.baseColorTexture.uuid);
                p.normalTex       = resolveBindlessTextureIndex(m.core.pbrMR.normalTexture.uuid);
                p.mrTex           = resolveBindlessTextureIndex(m.core.pbrMR.metallicRoughnessTexture.uuid);
                p.occlusionTex    = resolveBindlessTextureIndex(m.core.pbrMR.ambientOcclusionTexture.uuid);
                p.emissiveTex     = resolveBindlessTextureIndex(m.core.pbrMR.emissiveTexture.uuid);
                blockOffset       = allocBlock(&p, sizeof(p));
                break;
            }
            case vasset::VMaterialModel::eUnlit: {
                gm.model = GpuMaterialModel::eUnlit;
                MaterialParamsUnlit p;
                p.color     = m.core.unlit.color;
                p.colorTex  = resolveBindlessTextureIndex(m.core.unlit.colorTexture.uuid);
                blockOffset = allocBlock(&p, sizeof(p));
                break;
            }
            case vasset::VMaterialModel::ePhong:
            default: {
                gm.model = GpuMaterialModel::ePhong;
                MaterialParamsPhong p;
                p.diffuse           = m.core.phong.diffuse;
                p.specularShininess = glm::vec4(m.core.phong.specular, m.core.phong.shininess);
                p.diffuseTex        = resolveBindlessTextureIndex(m.core.phong.diffuseTexture.uuid);
                blockOffset         = allocBlock(&p, sizeof(p));
                break;
            }
        }

        gm.blockOffsetBytes = blockOffset;
        gm.tableIndex       = static_cast<uint32_t>(m_Scene.materials.size());
        m_Scene.materials.push_back(gm);
        return gm.tableIndex;
    }

    uint32_t AssetSystem::uploadMesh(const vasset::VMesh& cpuMesh, uint32_t materialOffset)
    {
        if (!m_RenderDevice)
            return std::numeric_limits<uint32_t>::max();

        resource::GpuMesh out;

        out.vertexCount = cpuMesh.vertexCount;
        out.indexCount  = static_cast<uint32_t>(cpuMesh.indices.size());

        // Build a tightly packed vertex stream for now: position/normal/uv0
        struct Vertex
        {
            glm::vec3 pos;
            glm::vec3 nrm;
            glm::vec2 uv0;
        };

        std::vector<Vertex> vertices;
        vertices.resize(cpuMesh.vertexCount);

        for (uint32_t i = 0; i < cpuMesh.vertexCount; ++i)
        {
            vertices[i].pos = cpuMesh.positions[i];
            if (!cpuMesh.normals.empty())
                vertices[i].nrm = cpuMesh.normals[i];
            else
                vertices[i].nrm = {0, 1, 0};

            if (!cpuMesh.texCoords0.empty())
                vertices[i].uv0 = cpuMesh.texCoords0[i];
            else
                vertices[i].uv0 = {0, 0};
        }

        out.vertexBuffer = m_RenderDevice->createVertexBuffer(sizeof(Vertex), vertices.size() * sizeof(Vertex));
        m_RenderDevice->upload(out.vertexBuffer, 0, vertices.size() * sizeof(Vertex), vertices.data());

        out.indexBuffer =
            m_RenderDevice->createIndexBuffer(rhi::IndexType::eUInt32, cpuMesh.indices.size() * sizeof(uint32_t));
        m_RenderDevice->upload(out.indexBuffer, 0, cpuMesh.indices.size() * sizeof(uint32_t), cpuMesh.indices.data());

        // Draw data buffer: one record per submesh (indexOffset/indexCount/materialIndex)
        struct DrawRange
        {
            uint32_t indexOffset;
            uint32_t indexCount;
            uint32_t materialIndex; // scene material table index
            uint32_t pad;
        };

        std::vector<DrawRange> ranges;
        ranges.reserve(cpuMesh.subMeshes.size());
        for (const auto& sm : cpuMesh.subMeshes)
        {
            DrawRange r;
            r.indexOffset   = sm.indexOffset;
            r.indexCount    = sm.indexCount;
            r.materialIndex = materialOffset + sm.materialIndex;
            r.pad           = 0;
            ranges.push_back(r);
        }

        out.drawDataBuffer = m_RenderDevice->createStorageBuffer(ranges.size() * sizeof(DrawRange));
        if (!ranges.empty())
            m_RenderDevice->upload(out.drawDataBuffer, 0, ranges.size() * sizeof(DrawRange), ranges.data());

        out.materialOffset = materialOffset;
        out.materialCount  = static_cast<uint32_t>(cpuMesh.materials.size());

        const uint32_t idx = static_cast<uint32_t>(m_Scene.meshes.size());
        m_Scene.meshes.push_back(std::move(out));
        return idx;
    }

    AssetHandle<vasset::VTexture, resource::GpuTexture> AssetSystem::loadTextureSync(const CoreUUID& uuid)
    {
        auto* rec = m_TextureCache.findOrCreate(uuid);
        if (!rec)
            return {};

        if (rec->gpuIndex.load(std::memory_order_acquire) != std::numeric_limits<uint32_t>::max())
            return AssetHandle<vasset::VTexture, resource::GpuTexture>(rec);

        std::filesystem::path path;
        if (!resolveUUIDToPath(uuid, path))
        {
            VULTRA_CLIENT_ERROR("loadTextureSync: cannot resolve uuid {}", vbase::to_string(uuid));
            return AssetHandle<vasset::VTexture, resource::GpuTexture>(rec);
        }

        auto bytes = readFileBytes(path);
        if (bytes.empty())
        {
            VULTRA_CLIENT_ERROR("loadTextureSync: failed to read {}", path.string());
            return AssetHandle<vasset::VTexture, resource::GpuTexture>(rec);
        }

        auto cpu = std::make_unique<vasset::VTexture>();
        auto r   = vasset::loadTextureFromMemory(bytes, *cpu);
        if (!r)
        {
            VULTRA_CLIENT_ERROR("loadTextureSync: vasset::loadTextureFromMemory failed: {}", path.string());
            return AssetHandle<vasset::VTexture, resource::GpuTexture>(rec);
        }

        const uint32_t gpuIndex = uploadTexture(*cpu);

        // cache bindless (gpuIndex is the bindless slot)
        m_TexUUIDToBindlessIndex[uuid] = gpuIndex;

        // store
        rec->cpu = m_Desc.keepCpuCopy ? std::move(cpu) : nullptr;
        rec->gpuIndex.store(gpuIndex, std::memory_order_release);
        rec->state.store(AssetState::eLoaded, std::memory_order_release);

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

        if (rec->gpuIndex.load(std::memory_order_acquire) != std::numeric_limits<uint32_t>::max())
            return AssetHandle<vasset::VMesh, resource::GpuMesh>(rec);

        std::filesystem::path path;
        if (!resolveUUIDToPath(uuid, path))
        {
            VULTRA_CLIENT_ERROR("loadMeshSync: cannot resolve uuid {}", vbase::to_string(uuid));
            return AssetHandle<vasset::VMesh, resource::GpuMesh>(rec);
        }

        auto bytes = readFileBytes(path);
        if (bytes.empty())
        {
            VULTRA_CLIENT_ERROR("loadMeshSync: failed to read {}", path.string());
            return AssetHandle<vasset::VMesh, resource::GpuMesh>(rec);
        }

        auto cpu = std::make_unique<vasset::VMesh>();
        auto r   = vasset::loadMeshFromMemory(bytes, *cpu);
        if (!r)
        {
            VULTRA_CLIENT_ERROR("loadMeshSync: vasset::loadMeshFromMemory failed: {}", path.string());
            return AssetHandle<vasset::VMesh, resource::GpuMesh>(rec);
        }

        // Create materials first (they may trigger texture loading)
        const uint32_t materialOffset = static_cast<uint32_t>(m_Scene.materials.size());
        for (const auto& mat : cpu->materials)
        {
            createAndAppendGpuMaterial(mat);
        }

        const uint32_t meshIndex = uploadMesh(*cpu, materialOffset);

        // store
        rec->cpu = m_Desc.keepCpuCopy ? std::move(cpu) : nullptr;
        rec->gpuIndex.store(meshIndex, std::memory_order_release);
        rec->state.store(AssetState::eLoaded, std::memory_order_release);

        return AssetHandle<vasset::VMesh, resource::GpuMesh>(rec);
    }

    AssetHandle<vasset::VMesh, resource::GpuMesh> AssetSystem::loadMeshSync(std::string_view uri)
    {
        CoreUUID uuid {};
        if (!resolveUriToUUID(uri, uuid))
            return {};
        return loadMeshSync(uuid);
    }
} // namespace vultra
