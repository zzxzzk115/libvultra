#include "vultra/function/asset/asset_system.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/function/resource/vtexture_loader.hpp"
#include "vultra/function/services/render_backend_service.hpp"

#include <vasset/editor_filesystem.hpp>
#include <vasset/vasset_importers.hpp>
#include <vasset/vmaterial.hpp>

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

            auto add = [&](LocationIndex loc, VertexAttribute::Type type) {
                attrs[loc] = {type, offset};

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

        std::vector<uint8_t> packVertices(const VMesh& mesh, const GpuVertexLayout& layout)
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
            auto registryPath = (std::filesystem::path(m_Desc.assetRoot) / m_Desc.importedFolder / m_Desc.registryFile)
                                    .generic_string();
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

        // auto mesh = loadMeshSync("res://models/DamagedHelmet/DamagedHelmet.gltf");
    }

    bool AssetSystem::resolveUUIDToUri(const CoreUUID& uuid, std::string& outUri) const
    {
        auto entry = m_Registry.lookup(uuid);
        if (entry.type == vasset::VAssetType::eUnknown)
            return false;

        // Must be the imported path, not the source path
        // Hence why, we pack it ourselves rather than using UUIDResolver::resolve.
        outUri = m_Desc.scheme + "://" + entry.importedPath;
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

        // Bindless ownership is in GpuScene.
        resource::GpuTexture out;
        out.texture = createRef<rhi::Texture>(std::move(tr.value()));
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
        gm.tableIndex       = static_cast<uint32_t>(m_Scene.materials.size());
        m_Scene.materials.push_back(gm);
        return gm.tableIndex;
    }

    uint32_t AssetSystem::uploadMesh(const vasset::VMesh& cpuMesh, uint32_t materialOffset)
    {
        if (!m_RenderDevice)
            return std::numeric_limits<uint32_t>::max();

        resource::GpuMesh out;

        out.vertexCount       = cpuMesh.vertexCount;
        out.indexCount        = static_cast<uint32_t>(cpuMesh.indices.size());
        out.layout.attributes = buildVertexAttributes(cpuMesh.vertexFlags, out.layout.stride);

        auto vertexData = packVertices(cpuMesh, out.layout);
        // ================================
        // Vertex buffer
        // ================================
        out.vertexBuffer = m_RenderDevice->createVertexBuffer(out.layout.stride, vertexData.size());

        auto vertexStaging = m_RenderDevice->createStagingBuffer(vertexData.size(), vertexData.data());

        m_RenderDevice->execute(
            [&](rhi::CommandBuffer& cb) {
                cb.copyBuffer(vertexStaging, out.vertexBuffer, vk::BufferCopy {0, 0, vertexData.size()});
            },
            true);

        // ================================
        // Index buffer
        // ================================

        const size_t indexSize = cpuMesh.indices.size() * sizeof(uint32_t);

        out.indexBuffer = m_RenderDevice->createIndexBuffer(rhi::IndexType::eUInt32, indexSize);

        auto indexStaging = m_RenderDevice->createStagingBuffer(indexSize, cpuMesh.indices.data());

        m_RenderDevice->execute(
            [&](rhi::CommandBuffer& cb) {
                cb.copyBuffer(indexStaging, out.indexBuffer, vk::BufferCopy {0, 0, indexSize});
            },
            true);

        // ================================
        // Draw data buffer
        // ================================

        struct DrawRange
        {
            uint32_t indexOffset;
            uint32_t indexCount;
            uint32_t materialIndex;
            uint32_t pad;
        };

        std::vector<DrawRange> ranges;
        ranges.reserve(cpuMesh.subMeshes.size());

        for (const auto& sm : cpuMesh.subMeshes)
        {
            ranges.push_back({sm.indexOffset, sm.indexCount, materialOffset + sm.materialIndex, 0});
        }

        const size_t drawSize = ranges.size() * sizeof(DrawRange);

        out.drawDataBuffer = m_RenderDevice->createStorageBuffer(drawSize);

        if (drawSize > 0)
        {
            auto staging = m_RenderDevice->createStagingBuffer(drawSize, ranges.data());

            m_RenderDevice->execute(
                [&](rhi::CommandBuffer& cb) {
                    cb.copyBuffer(staging, out.drawDataBuffer, vk::BufferCopy {0, 0, drawSize});
                },
                true);
        }

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

        std::string uri;
        if (!resolveUUIDToUri(uuid, uri))
        {
            VULTRA_CLIENT_ERROR("loadTextureSync: cannot resolve uuid {}", vbase::to_string(uuid));
            return AssetHandle<vasset::VTexture, resource::GpuTexture>(rec);
        }

        auto br = m_VFS.readAll(uri);
        if (!br)
        {
            VULTRA_CLIENT_ERROR("loadTextureSync: failed to read {}", uri);
            return AssetHandle<vasset::VTexture, resource::GpuTexture>(rec);
        }

        const auto& bytes = br.value();

        auto cpu = std::make_unique<vasset::VTexture>();
        auto r   = vasset::loadTextureFromMemory(bytes, *cpu);
        if (!r)
        {
            VULTRA_CLIENT_ERROR("loadTextureSync: vasset::loadTextureFromMemory failed: {}", uri);
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

        std::string uri;
        if (!resolveUUIDToUri(uuid, uri))
        {
            VULTRA_CLIENT_ERROR("loadMeshSync: cannot resolve uuid {}", vbase::to_string(uuid));
            return AssetHandle<vasset::VMesh, resource::GpuMesh>(rec);
        }

        auto br = m_VFS.readAll(uri);
        if (!br)
        {
            VULTRA_CLIENT_ERROR("loadMeshSync: failed to read {}", uri);
            return AssetHandle<vasset::VMesh, resource::GpuMesh>(rec);
        }

        const auto& bytes = br.value();

        auto cpu = std::make_unique<vasset::VMesh>();
        auto r   = vasset::loadMeshFromMemory(bytes, *cpu);
        if (!r)
        {
            VULTRA_CLIENT_ERROR("loadMeshSync: vasset::loadMeshFromMemory failed: {}", uri);
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

    std::string AssetSystem::resolveUri(const std::string_view uri) const
    {
        auto vbaseUri = vfilesystem::parse_uri(uri);
        return m_Desc.assetRoot + vbaseUri.path.str().data();
    }
} // namespace vultra
