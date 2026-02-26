#include "vultra/function/asset/asset_system.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/vertex_attributes.hpp"
#include "vultra/function/resource/vtexture_loader.hpp"
#include "vultra/function/services/render_backend_service.hpp"

#include <vasset/editor_filesystem.hpp>
#include <vasset/vasset_importers.hpp>
#include <vasset/vmaterial.hpp>

#include <cstring>
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

        // auto mesh = loadMeshSync("res://models/DamagedHelmet/DamagedHelmet.gltf");
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
                    // - Upload mesh buffers + append to m_Scene
                    // - Store gpuIndex and transition to eReady
                    //
                    // For now we keep the original sync upload logic here so rendering bring-up keeps working.

                    if (rec->cpu)
                    {
                        const uint32_t materialOffset = static_cast<uint32_t>(m_Scene.materials.size());
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
            }
        }

        // GC hook (TODO): Use frameIndex + refCount/lastUsedFrame to evict CPU/GPU if desired.
        (void)frameIndex;
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

        out.vertexCount = cpuMesh.vertexCount;
        out.indexCount  = static_cast<uint32_t>(cpuMesh.indices.size());

        // ------------------------------------------------------------
        // Build dynamic AoS vertex layout from vasset::VMesh vertexFlags.
        // Tightly-packed AoS reduces binds and copy commands.
        // rhi::VertexAttributes is attached to GpuMesh for pipeline creation.
        // ------------------------------------------------------------

        rhi::VertexAttributes attribs;

        // Stable semantic locations (match shader conventions).
        constexpr uint32_t kLocPosition = 0;
        constexpr uint32_t kLocNormal   = 1;
        constexpr uint32_t kLocColor0   = 2;
        constexpr uint32_t kLocUV0      = 3;
        constexpr uint32_t kLocUV1      = 4;
        constexpr uint32_t kLocTangent  = 5;
        constexpr uint32_t kLocJoints0  = 6;
        constexpr uint32_t kLocWeights0 = 7;

        const auto flags      = cpuMesh.vertexFlags;
        const bool hasPos     = (flags & vasset::VVertexFlags::ePosition);
        const bool hasNormal  = (flags & vasset::VVertexFlags::eNormal);
        const bool hasColor   = (flags & vasset::VVertexFlags::eColor);
        const bool hasUV0     = (flags & vasset::VVertexFlags::eTexCoord0);
        const bool hasUV1     = (flags & vasset::VVertexFlags::eTexCoord1);
        const bool hasTangent = (flags & vasset::VVertexFlags::eTangent);
        const bool hasJoints  = (flags & vasset::VVertexFlags::eJointIndices);
        const bool hasWeights = (flags & vasset::VVertexFlags::eJointWeights);

        uint32_t stride = 0;

        // POSITION (required for renderable meshes)
        if (hasPos)
        {
            attribs.emplace(kLocPosition, rhi::VertexAttribute {rhi::VertexAttribute::Type::eFloat3, stride});
            stride += rhi::getSize(rhi::VertexAttribute::Type::eFloat3);
        }
        else
        {
            VULTRA_CORE_WARN("VMesh has no positions; uploading an empty vertex layout.");
        }

        // NORMAL (optional)
        if (hasNormal)
        {
            attribs.emplace(kLocNormal, rhi::VertexAttribute {rhi::VertexAttribute::Type::eFloat3, stride});
            stride += rhi::getSize(rhi::VertexAttribute::Type::eFloat3);
        }

        // COLOR0 (optional)
        if (hasColor)
        {
            attribs.emplace(kLocColor0, rhi::VertexAttribute {rhi::VertexAttribute::Type::eFloat3, stride});
            stride += rhi::getSize(rhi::VertexAttribute::Type::eFloat3);
        }

        // UV0 (optional)
        if (hasUV0)
        {
            attribs.emplace(kLocUV0, rhi::VertexAttribute {rhi::VertexAttribute::Type::eFloat2, stride});
            stride += rhi::getSize(rhi::VertexAttribute::Type::eFloat2);
        }

        // UV1 (optional)
        if (hasUV1)
        {
            attribs.emplace(kLocUV1, rhi::VertexAttribute {rhi::VertexAttribute::Type::eFloat2, stride});
            stride += rhi::getSize(rhi::VertexAttribute::Type::eFloat2);
        }

        // TANGENT (optional)
        if (hasTangent)
        {
            attribs.emplace(kLocTangent, rhi::VertexAttribute {rhi::VertexAttribute::Type::eFloat4, stride});
            stride += rhi::getSize(rhi::VertexAttribute::Type::eFloat4);
        }

        // JOINTS0 (optional)
        if (hasJoints)
        {
            attribs.emplace(kLocJoints0, rhi::VertexAttribute {rhi::VertexAttribute::Type::eFloat4, stride});
            stride += rhi::getSize(rhi::VertexAttribute::Type::eFloat4);
        }

        // WEIGHTS0 (optional)
        if (hasWeights)
        {
            attribs.emplace(kLocWeights0, rhi::VertexAttribute {rhi::VertexAttribute::Type::eFloat4, stride});
            stride += rhi::getSize(rhi::VertexAttribute::Type::eFloat4);
        }

        out.vertexAttributes = attribs;

        // ------------------------------------------------------------
        // Pack AoS vertex blob
        // ------------------------------------------------------------

        const uint32_t vertexCount = cpuMesh.vertexCount;
        const size_t   vertexSize  = static_cast<size_t>(vertexCount) * static_cast<size_t>(stride);

        std::vector<std::byte> vertexBlob;
        vertexBlob.resize(vertexSize);

        auto write_bytes = [&](std::byte* dst, const void* src, size_t n) { std::memcpy(dst, src, n); };

        for (uint32_t i = 0; i < vertexCount; ++i)
        {
            std::byte* vdst = vertexBlob.data() + static_cast<size_t>(i) * stride;

            // POSITION
            if (auto it = attribs.find(kLocPosition); it != attribs.end())
            {
                const vasset::VPosition pos =
                    (i < cpuMesh.positions.size()) ? cpuMesh.positions[i] : vasset::VPosition(0);
                write_bytes(vdst + it->second.offset, &pos, sizeof(vasset::VPosition));
            }

            // NORMAL
            if (auto it = attribs.find(kLocNormal); it != attribs.end())
            {
                const vasset::VNormal nrm =
                    (i < cpuMesh.normals.size()) ? cpuMesh.normals[i] : vasset::VNormal(0, 1, 0);
                write_bytes(vdst + it->second.offset, &nrm, sizeof(vasset::VNormal));
            }

            // COLOR0
            if (auto it = attribs.find(kLocColor0); it != attribs.end())
            {
                const vasset::VColor c = (i < cpuMesh.colors.size()) ? cpuMesh.colors[i] : vasset::VColor(1, 1, 1);
                write_bytes(vdst + it->second.offset, &c, sizeof(vasset::VColor));
            }

            // UV0
            if (auto it = attribs.find(kLocUV0); it != attribs.end())
            {
                const vasset::VTexCoord uv0 =
                    (i < cpuMesh.texCoords0.size()) ? cpuMesh.texCoords0[i] : vasset::VTexCoord(0);
                write_bytes(vdst + it->second.offset, &uv0, sizeof(vasset::VTexCoord));
            }

            // UV1
            if (auto it = attribs.find(kLocUV1); it != attribs.end())
            {
                const vasset::VTexCoord uv1 =
                    (i < cpuMesh.texCoords1.size()) ? cpuMesh.texCoords1[i] : vasset::VTexCoord(0);
                write_bytes(vdst + it->second.offset, &uv1, sizeof(vasset::VTexCoord));
            }

            // TANGENT
            if (auto it = attribs.find(kLocTangent); it != attribs.end())
            {
                const vasset::VTangent t = (i < cpuMesh.tangents.size()) ? cpuMesh.tangents[i] : vasset::VTangent(0);
                write_bytes(vdst + it->second.offset, &t, sizeof(vasset::VTangent));
            }

            // JOINTS0
            if (auto it = attribs.find(kLocJoints0); it != attribs.end())
            {
                const vasset::VJointIndices ji =
                    (i < cpuMesh.jointIndices.size()) ? cpuMesh.jointIndices[i] : vasset::VJointIndices(0);
                write_bytes(vdst + it->second.offset, &ji, sizeof(vasset::VJointIndices));
            }

            // WEIGHTS0
            if (auto it = attribs.find(kLocWeights0); it != attribs.end())
            {
                const vasset::VJointWeights jw =
                    (i < cpuMesh.jointWeights.size()) ? cpuMesh.jointWeights[i] : vasset::VJointWeights(0);
                write_bytes(vdst + it->second.offset, &jw, sizeof(vasset::VJointWeights));
            }
        }

        // ------------------------------------------------------------
        // Create GPU buffers
        // ------------------------------------------------------------

        out.vertexBuffer = m_RenderDevice->createVertexBuffer(stride, vertexSize);

        const size_t indexSize = cpuMesh.indices.size() * sizeof(uint32_t);
        out.indexBuffer        = m_RenderDevice->createIndexBuffer(rhi::IndexType::eUInt32, indexSize);

        // ------------------------------------------------------------
        // Build draw data buffer (ranges)
        // ------------------------------------------------------------

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
        out.drawDataBuffer    = m_RenderDevice->createStorageBuffer(drawSize);

        // ------------------------------------------------------------
        // Upload via a single one-time command buffer submission.
        // Staging is intentionally split by resource type:
        //   - vertex staging (AoS blob)
        //   - index staging
        //   - draw range staging
        // This keeps lifetimes/updates decoupled (future GPU-driven & streaming).
        // ------------------------------------------------------------

        rhi::Buffer vertexStaging;
        rhi::Buffer indexStaging;
        rhi::Buffer drawStaging;

        if (vertexSize > 0)
            vertexStaging = m_RenderDevice->createStagingBuffer(vertexSize, vertexBlob.data());
        if (indexSize > 0)
            indexStaging = m_RenderDevice->createStagingBuffer(indexSize, cpuMesh.indices.data());
        if (drawSize > 0)
            drawStaging = m_RenderDevice->createStagingBuffer(drawSize, ranges.data());

        m_RenderDevice->execute(
            [&](rhi::CommandBuffer& cb) {
                if (vertexSize > 0)
                {
                    cb.copyBuffer(vertexStaging, out.vertexBuffer, vk::BufferCopy {0, 0, vertexSize});
                }
                if (indexSize > 0)
                {
                    cb.copyBuffer(indexStaging, out.indexBuffer, vk::BufferCopy {0, 0, indexSize});
                }
                if (drawSize > 0)
                {
                    cb.copyBuffer(drawStaging, out.drawDataBuffer, vk::BufferCopy {0, 0, drawSize});
                }

                // TODO (upload pipeline):
                // - add transfer->vertex/index/storage buffer barriers if required
                // - optionally batch multiple assets per execute() when async CPU loading is introduced
            },
            true);

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

        if (rec->state.load(std::memory_order_acquire) == AssetState::eReady &&
            rec->gpuIndex.load(std::memory_order_acquire) != std::numeric_limits<uint32_t>::max())
        {
            return AssetHandle<vasset::VTexture, resource::GpuTexture>(rec);
        }

        // CPU stage (sync baseline)
        if (rec->state.load(std::memory_order_acquire) == AssetState::eUnloaded)
        {
            rec->state.store(AssetState::eLoadingCPU, std::memory_order_release);

            std::filesystem::path path;
            if (!resolveUUIDToPath(uuid, path))
            {
                VULTRA_CLIENT_ERROR("loadTextureSync: cannot resolve uuid {}", uuid.toString());
                rec->state.store(AssetState::eFailed, std::memory_order_release);
                return AssetHandle<vasset::VTexture, resource::GpuTexture>(rec);
            }

            auto bytes = readFileBytes(path);
            if (bytes.empty())
            {
                VULTRA_CLIENT_ERROR("loadTextureSync: failed to read {}", path.string());
                rec->state.store(AssetState::eFailed, std::memory_order_release);
                return AssetHandle<vasset::VTexture, resource::GpuTexture>(rec);
            }

            auto cpu = std::make_unique<vasset::VTexture>();
            auto r   = vasset::loadTextureFromMemory(bytes, *cpu);
            if (!r)
            {
                VULTRA_CLIENT_ERROR("loadTextureSync: vasset::loadTextureFromMemory failed: {}", path.string());
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

            std::filesystem::path path;
            if (!resolveUUIDToPath(uuid, path))
            {
                VULTRA_CLIENT_ERROR("loadMeshSync: cannot resolve uuid {}", uuid.toString());
                rec->state.store(AssetState::eFailed, std::memory_order_release);
                return AssetHandle<vasset::VMesh, resource::GpuMesh>(rec);
            }

            auto bytes = readFileBytes(path);
            if (bytes.empty())
            {
                VULTRA_CLIENT_ERROR("loadMeshSync: failed to read {}", path.string());
                rec->state.store(AssetState::eFailed, std::memory_order_release);
                return AssetHandle<vasset::VMesh, resource::GpuMesh>(rec);
            }

            auto cpu = std::make_unique<vasset::VMesh>();
            auto r   = vasset::loadMeshFromMemory(bytes, *cpu);
            if (!r)
            {
                VULTRA_CLIENT_ERROR("loadMeshSync: vasset::loadMeshFromMemory failed: {}", path.string());
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

    std::string AssetSystem::resolveUri(const std::string_view uri) const
    {
        auto vbaseUri = vfilesystem::parse_uri(uri);
        return m_Desc.assetRoot + vbaseUri.path.str().data();
    }
} // namespace vultra
