#include "vultra/function/renderer/mesh_loader.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/graphics_pipeline.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/util.hpp"
#include "vultra/function/renderer/mesh_manager.hpp"
#include "vultra/function/renderer/texture_manager.hpp"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <magic_enum.hpp>

namespace
{
    const vultra::rhi::BlendState kNoBlend = {
        .enabled = false,
    };

    const vultra::rhi::BlendState kAlphaBlend = {
        .enabled  = true,
        .srcColor = vultra::rhi::BlendFactor::eSrcAlpha,
        .dstColor = vultra::rhi::BlendFactor::eOneMinusSrcAlpha,
        .colorOp  = vultra::rhi::BlendOp::eAdd,
        .srcAlpha = vultra::rhi::BlendFactor::eOne,
        .dstAlpha = vultra::rhi::BlendFactor::eZero,
        .alphaOp  = vultra::rhi::BlendOp::eAdd,
    };

    const vultra::rhi::BlendState kAdditiveBlend = {
        .enabled  = true,
        .srcColor = vultra::rhi::BlendFactor::eOne,
        .dstColor = vultra::rhi::BlendFactor::eOne,
        .colorOp  = vultra::rhi::BlendOp::eAdd,
        .srcAlpha = vultra::rhi::BlendFactor::eOne,
        .dstAlpha = vultra::rhi::BlendFactor::eZero,
        .alphaOp  = vultra::rhi::BlendOp::eAdd,
    };

    vultra::rhi::BlendState toBlendState(aiBlendMode mode)
    {
        switch (mode)
        {
            case aiBlendMode_Default:
                return kAlphaBlend;
            case aiBlendMode_Additive:
                return kAdditiveBlend;
            default:
                VULTRA_CORE_WARN("[MeshLoader] Unknown aiBlendMode {}, using default blend state",
                                 magic_enum::enum_name(mode).data());
                return toBlendState(aiBlendMode::aiBlendMode_Default);
        }
    }
} // namespace

namespace vultra
{
    namespace gfx
    {
        entt::resource_loader<MeshResource>::result_type MeshLoader::operator()(const std::filesystem::path& p,
                                                                                rhi::RenderDevice&           rd)
        {
            if (m_DefaultWhite1x1 == nullptr)
            {
                m_DefaultWhite1x1 = createDefaultTexture(255, 255, 255, 255, rd);
            }

            Assimp::Importer importer;
            const aiScene*   scene = importer.ReadFile(
                p.generic_string(), aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_PreTransformVertices);

            if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode || !scene->HasMeshes())
            {
                VULTRA_CORE_ERROR("[MeshLoader] Assimp error: {}", importer.GetErrorString());
                return {};
            }

            DefaultMesh mesh {};
            mesh.vertexFormat = SimpleVertex::getVertexFormat();

            uint32_t vertexOffset = 0;
            uint32_t indexOffset  = 0;

            const auto loadTexture = [this, &p](const aiMaterial*   material,
                                                const aiTextureType type) -> Ref<rhi::Texture> {
                assert(material);

                if (material->GetTextureCount(type) > 0)
                {
                    aiString path {};
                    material->GetTexture(type, 0, &path);
                    // If using optimized textures, try to load .ktx2 first
                    if (MeshManager::getGlobalLoadingSettings().useOptimizedTextures)
                    {
                        auto basisPath = ("imported" / p.parent_path() / path.data).replace_extension(".ktx2");
                        if (std::filesystem::exists(basisPath))
                        {
                            return resource::loadResource<TextureManager>(basisPath.generic_string());
                        }
                    }
                    return resource::loadResource<TextureManager>((p.parent_path() / path.data).generic_string());
                }

                VULTRA_CORE_WARN("[MeshLoader] Material has no texture of type {}", magic_enum::enum_name(type).data());
                return nullptr;
            };

            const auto processMesh = [this, &mesh, loadTexture](const aiMesh*  aiMesh,
                                                                const aiScene* aiScene,
                                                                uint32_t&      vertexOffset,
                                                                uint32_t&      indexOffset) {
                // Copy vertices
                for (unsigned int v = 0; v < aiMesh->mNumVertices; ++v)
                {
                    SimpleVertex vertex {};
                    vertex.position = {aiMesh->mVertices[v].x, aiMesh->mVertices[v].y, aiMesh->mVertices[v].z};

                    if (aiMesh->HasVertexColors(0))
                    {
                        vertex.color = {aiMesh->mColors[0][v].r, aiMesh->mColors[0][v].g, aiMesh->mColors[0][v].b};
                    }
                    else
                    {
                        vertex.color = {1.0f, 1.0f, 1.0f};
                    }

                    if (aiMesh->HasNormals())
                    {
                        vertex.normal = {aiMesh->mNormals[v].x, aiMesh->mNormals[v].y, aiMesh->mNormals[v].z};
                    }

                    if (aiMesh->HasTextureCoords(0))
                    {
                        vertex.texCoord = {aiMesh->mTextureCoords[0][v].x, aiMesh->mTextureCoords[0][v].y};
                    }

                    if (aiMesh->HasTangentsAndBitangents())
                    {
                        glm::vec3 tangent   = {aiMesh->mTangents[v].x, aiMesh->mTangents[v].y, aiMesh->mTangents[v].z};
                        glm::vec3 bitangent = {
                            aiMesh->mBitangents[v].x, aiMesh->mBitangents[v].y, aiMesh->mBitangents[v].z};
                        glm::vec4 tangentWithHandness = glm::vec4(
                            tangent, glm::dot(glm::cross(vertex.normal, bitangent), tangent) < 0.0f ? -1.0f : 1.0f);
                        vertex.tangent = tangentWithHandness;
                    }

                    mesh.vertices.push_back(vertex);
                }

                // Copy indices
                for (unsigned int f = 0; f < aiMesh->mNumFaces; ++f)
                {
                    const aiFace& face = aiMesh->mFaces[f];
                    assert(face.mNumIndices == 3); // Ensure the mesh is triangulated
                    mesh.indices.push_back(face.mIndices[0]);
                    mesh.indices.push_back(face.mIndices[1]);
                    mesh.indices.push_back(face.mIndices[2]);
                }

                // Fill in sub-mesh data
                DefaultSubMesh subMesh {};
                // subMesh.topology = static_cast<rhi::PrimitiveTopology>(aiMesh->mPrimitiveTypes);
                subMesh.vertexOffset = vertexOffset;
                subMesh.indexOffset  = indexOffset;
                subMesh.vertexCount  = aiMesh->mNumVertices;
                subMesh.indexCount   = aiMesh->mNumFaces * 3;
                // Handle materials
                if (aiMesh->mMaterialIndex >= 0)
                {
                    const aiMaterial* material = aiScene->mMaterials[aiMesh->mMaterialIndex];

                    // Log material properties
                    aiString materialName;
                    auto     result = material->Get(AI_MATKEY_NAME, materialName);
                    if (result != aiReturn_SUCCESS)
                    {
                        VULTRA_CORE_WARN("[MeshLoader] Failed to get material name");
                    }
                    else
                    {
                        VULTRA_CORE_TRACE("[MeshLoader] Loading material: {}", materialName.C_Str());
                        subMesh.material.name = materialName.C_Str();
                    }

#if DEBUG
                    // Traverse material properties
                    for (unsigned int i = 0; i < material->mNumProperties; ++i)
                    {
                        aiMaterialProperty* prop = material->mProperties[i];
                        VULTRA_CORE_TRACE(
                            "[MeshLoader] MaterialProperty {}: key='{}', semantic={}, index={}, type={}, dataLength={}",
                            i,
                            prop->mKey.C_Str(),
                            prop->mSemantic,
                            prop->mIndex,
                            magic_enum::enum_name(prop->mType).data(),
                            prop->mDataLength);
                    }
#endif

                    // Albedo
                    auto albedoTexture      = loadTexture(material, aiTextureType_DIFFUSE);
                    subMesh.material.albedo = albedoTexture == nullptr ? m_DefaultWhite1x1 : albedoTexture;
                    aiColor4D baseColor;
                    result = material->Get(AI_MATKEY_BASE_COLOR, baseColor);
                    if (result != aiReturn_SUCCESS)
                    {
                        VULTRA_CORE_WARN("[MeshLoader] Failed to get base color");
                        subMesh.material.baseColor = glm::vec4(1, 1, 1, 1);
                    }
                    else
                    {
                        subMesh.material.baseColor = glm::vec4(baseColor.r, baseColor.g, baseColor.b, baseColor.a);
                    }
                    subMesh.material.useAlbedoTexture = (albedoTexture != nullptr);

                    // Alpha Mask
                    auto alphaMaskTexture      = loadTexture(material, aiTextureType_OPACITY);
                    subMesh.material.alphaMask = alphaMaskTexture == nullptr ? m_DefaultWhite1x1 : alphaMaskTexture;
                    subMesh.material.useAlphaMaskTexture = (alphaMaskTexture != nullptr);
                    if (alphaMaskTexture != nullptr)
                    {
                        VULTRA_CORE_TRACE("[MeshLoader] Material uses alpha mask texture");
                    }

                    // Opacity
                    float opacity = 1.0f;
                    result        = material->Get(AI_MATKEY_OPACITY, opacity);
                    if (result != aiReturn_SUCCESS)
                    {
                        VULTRA_CORE_WARN("[MeshLoader] Failed to get opacity");
                    }
                    else
                    {
                        subMesh.material.opacity    = opacity;
                        subMesh.material.blendState = (opacity < 1.0f) ? kAlphaBlend : kNoBlend;
                        if (opacity < 1.0f)
                        {
                            VULTRA_CORE_TRACE("[MeshLoader] Material is not opaque, opacity: {}", opacity);
                        }
                    }

                    // Alpha blend mode
                    aiBlendMode blendMode {aiBlendMode_Default};
                    result = material->Get(AI_MATKEY_BLEND_FUNC, blendMode);
                    if (result != aiReturn_SUCCESS)
                    {
                        VULTRA_CORE_WARN("[MeshLoader] Failed to get blend mode");
                    }
                    else
                    {
                        subMesh.material.blendState = toBlendState(blendMode);
                    }

                    // Metallic
                    auto metallicTexture      = loadTexture(material, aiTextureType_METALNESS);
                    subMesh.material.metallic = metallicTexture == nullptr ? m_DefaultWhite1x1 : metallicTexture;
                    result = material->Get(AI_MATKEY_METALLIC_FACTOR, subMesh.material.metallicFactor);
                    if (result != aiReturn_SUCCESS)
                    {
                        VULTRA_CORE_WARN("[MeshLoader] Failed to get metallic factor");
                        subMesh.material.metallicFactor = 0.0f;
                    }
                    subMesh.material.useMetallicTexture = (metallicTexture != nullptr);

                    // Roughness
                    auto roughnessTexture      = loadTexture(material, aiTextureType_DIFFUSE_ROUGHNESS);
                    subMesh.material.roughness = roughnessTexture == nullptr ? m_DefaultWhite1x1 : roughnessTexture;
                    result = material->Get(AI_MATKEY_ROUGHNESS_FACTOR, subMesh.material.roughnessFactor);
                    if (result != aiReturn_SUCCESS)
                    {
                        VULTRA_CORE_WARN("[MeshLoader] Failed to get roughness factor");
                        subMesh.material.roughnessFactor = 0.5f;
                    }
                    subMesh.material.useRoughnessTexture = (roughnessTexture != nullptr);

                    // Specular
                    auto specularTexture      = loadTexture(material, aiTextureType_SPECULAR);
                    subMesh.material.specular = specularTexture == nullptr ? m_DefaultWhite1x1 : specularTexture;
                    subMesh.material.useSpecularTexture = (specularTexture != nullptr);

                    // Normal
                    auto normalTexture                = loadTexture(material, aiTextureType_NORMALS);
                    subMesh.material.normal           = normalTexture == nullptr ? m_DefaultWhite1x1 : normalTexture;
                    subMesh.material.useNormalTexture = (normalTexture != nullptr);

                    // Ambient Occlusion
                    auto aoTexture                = loadTexture(material, aiTextureType_AMBIENT_OCCLUSION);
                    subMesh.material.ao           = aoTexture == nullptr ? m_DefaultWhite1x1 : aoTexture;
                    subMesh.material.useAOTexture = (aoTexture != nullptr);

                    // Emissive
                    auto emissiveTexture      = loadTexture(material, aiTextureType_EMISSIVE);
                    subMesh.material.emissive = emissiveTexture == nullptr ? m_DefaultWhite1x1 : emissiveTexture;
                    subMesh.material.useEmissiveTexture = (emissiveTexture != nullptr);

                    // Metallic Roughness
                    auto metallicRoughnessTexture = loadTexture(material, aiTextureType_GLTF_METALLIC_ROUGHNESS);
                    subMesh.material.metallicRoughness =
                        metallicRoughnessTexture == nullptr ? m_DefaultWhite1x1 : metallicRoughnessTexture;
                    subMesh.material.useMetallicRoughnessTexture = (metallicRoughnessTexture != nullptr);

                    // Double Sided
                    result = material->Get(AI_MATKEY_TWOSIDED, subMesh.material.doubleSided);
                    if (result != aiReturn_SUCCESS)
                    {
                        VULTRA_CORE_WARN("[MeshLoader] Failed to get double sided");
                        subMesh.material.doubleSided = true;
                    }
                }
                mesh.subMeshes.push_back(subMesh);

                vertexOffset += subMesh.vertexCount;
                indexOffset += subMesh.indexCount;
            };

            std::function<void(const aiNode*, const aiScene*)> processNode;
            processNode = [&processMesh, &processNode, &vertexOffset, &indexOffset](const aiNode*  aiNode,
                                                                                    const aiScene* aiScene) {
                for (uint32_t i = 0; i < aiNode->mNumMeshes; ++i)
                {
                    aiMesh* aiMesh = aiScene->mMeshes[aiNode->mMeshes[i]];
                    processMesh(aiMesh, aiScene, vertexOffset, indexOffset);
                }

                for (uint32_t i = 0; i < aiNode->mNumChildren; ++i)
                {
                    processNode(aiNode->mChildren[i], aiScene);
                }
            };

            processNode(scene->mRootNode, scene);

            mesh.vertexBuffer = createRef<rhi::VertexBuffer>(
                std::move(rd.createVertexBuffer(mesh.getVertexStride(), mesh.getVertexCount())));
            auto stagingVertexBuffer =
                rd.createStagingBuffer(mesh.getVertexStride() * mesh.getVertexCount(), mesh.vertices.data());

            rhi::Buffer stagingIndexBuffer {};
            if (mesh.indices.size() > 0)
            {
                mesh.indexBuffer = createRef<rhi::IndexBuffer>(std::move(
                    rd.createIndexBuffer(static_cast<rhi::IndexType>(mesh.getIndexStride()), mesh.getIndexCount())));
                stagingIndexBuffer =
                    rd.createStagingBuffer(mesh.getIndexStride() * mesh.getIndexCount(), mesh.indices.data());
            }

            rd.execute([&](rhi::CommandBuffer& cb) {
                cb.copyBuffer(
                    stagingVertexBuffer, *mesh.vertexBuffer, vk::BufferCopy {0, 0, stagingVertexBuffer.getSize()});
                if (stagingIndexBuffer)
                {
                    cb.copyBuffer(
                        stagingIndexBuffer, *mesh.indexBuffer, vk::BufferCopy {0, 0, stagingIndexBuffer.getSize()});
                }
            });

            return createRef<MeshResource>(std::move(mesh), p);
        }

        entt::resource_loader<MeshResource>::result_type MeshLoader::operator()(const std::string_view name,
                                                                                DefaultMesh&&          mesh) const
        {
            return createRef<MeshResource>(std::move(mesh), name);
        }

        Ref<rhi::Texture>
        MeshLoader::createDefaultTexture(uint8_t r, uint8_t g, uint8_t b, uint8_t a, rhi::RenderDevice& rd)
        {
            auto texture =
                createRef<rhi::Texture>(rhi::Texture::Builder {}
                                            .setExtent({1, 1})
                                            .setPixelFormat(rhi::PixelFormat::eRGBA8_UNorm)
                                            .setNumMipLevels(1)
                                            .setNumLayers(std::nullopt)
                                            .setUsageFlags(rhi::ImageUsage::eTransferDst | rhi::ImageUsage::eSampled)
                                            .setupOptimalSampler(true)
                                            .build(rd));

            const uint8_t pixelData[4] = {r, g, b, a};
            const auto    pixelSize    = sizeof(uint8_t);
            const auto    uploadSize   = 1 * 1 * 4 * pixelSize;

            const auto srcStagingBuffer = rd.createStagingBuffer(uploadSize, pixelData);

            rhi::upload(rd, srcStagingBuffer, {}, *texture, false);

            return texture;
        }
    } // namespace gfx
} // namespace vultra