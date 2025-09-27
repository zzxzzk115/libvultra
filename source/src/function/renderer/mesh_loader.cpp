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
                rd.m_LoadedTextures.push_back(m_DefaultWhite1x1); // Ensure the index 0 is always white 1x1 as fallback
            }

            Assimp::Importer importer;
            const aiScene*   scene = importer.ReadFile(p.generic_string(),
                                                     aiProcess_Triangulate | aiProcess_FlipUVs |
                                                         aiProcess_PreTransformVertices | aiProcess_CalcTangentSpace);

            if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode || !scene->HasMeshes())
            {
                VULTRA_CORE_ERROR("[MeshLoader] Assimp error: {}", importer.GetErrorString());
                return {};
            }

            DefaultMesh mesh {};
            mesh.vertexFormat = SimpleVertex::getVertexFormat();

            uint32_t vertexOffset = 0;
            uint32_t indexOffset  = 0;

            const auto loadTexture = [this, &p, &rd](const aiMaterial* material, const aiTextureType type) -> uint32_t {
                assert(material);

                // lambda to load a texture from a path
                auto loadTextureFromPath = [this, &rd](const std::filesystem::path& path) -> uint32_t {
                    auto texture = resource::loadResource<TextureManager>(path.generic_string());
                    if (texture)
                    {
                        // Find index if possible
                        for (uint32_t i = 0; i < rd.m_LoadedTextures.size(); ++i)
                        {
                            if (rd.m_LoadedTextures[i] == texture)
                                return i;
                        }

                        // Cache as new
                        rd.m_LoadedTextures.push_back(texture);
                        return static_cast<uint32_t>(rd.m_LoadedTextures.size() - 1);
                    }
                    return 0;
                };

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
                            return loadTextureFromPath(basisPath);
                        }
                    }

                    // Fallback to original texture path
                    return loadTextureFromPath((p.parent_path() / path.data).generic_string());
                }

                VULTRA_CORE_WARN("[MeshLoader] Material has no texture of type {}", magic_enum::enum_name(type).data());
                return 0; // Fallback to white 1x1
            };

            const auto processMesh = [this, &mesh, loadTexture](
                                         const aiMesh* aiMesh, uint32_t& vertexOffset, uint32_t& indexOffset) {
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

                // Cook AABB
                mesh.aabb = AABB::build(mesh.vertices);

                // Fill in sub-mesh data
                SubMesh subMesh {};
                // subMesh.topology = static_cast<rhi::PrimitiveTopology>(aiMesh->mPrimitiveTypes);
                subMesh.vertexOffset  = vertexOffset;
                subMesh.indexOffset   = indexOffset;
                subMesh.vertexCount   = aiMesh->mNumVertices;
                subMesh.indexCount    = aiMesh->mNumFaces * 3;
                subMesh.materialIndex = aiMesh->mMaterialIndex;
                mesh.subMeshes.push_back(subMesh);

                vertexOffset += subMesh.vertexCount;
                indexOffset += subMesh.indexCount;
            };

            const auto processMaterials = [this, &mesh, &loadTexture](const aiScene* aiScene) {
                assert(aiScene);

                mesh.materials.resize(aiScene->mNumMaterials);

                for (unsigned int i = 0; i < aiScene->mNumMaterials; ++i)
                {
                    const aiMaterial* material = aiScene->mMaterials[i];
                    if (!material)
                        continue;

                    PBRMaterial pbrMat {};

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
                        pbrMat.name = materialName.C_Str();
                    }

#if _DEBUG
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

                    // Legacy shading will be converted to PBR
                    aiColor3D kd(1, 1, 1), ks(0, 0, 0), ke(0, 0, 0);
                    float     Ns = 0.0f, d = 1.0f, Ni = 1.5f, emissiveIntensity = 1.0f;

                    material->Get(AI_MATKEY_COLOR_DIFFUSE, kd);
                    material->Get(AI_MATKEY_COLOR_SPECULAR, ks);
                    material->Get(AI_MATKEY_COLOR_EMISSIVE, ke);
                    material->Get(AI_MATKEY_EMISSIVE_INTENSITY, emissiveIntensity);
                    material->Get(AI_MATKEY_SHININESS, Ns);
                    material->Get(AI_MATKEY_OPACITY, d);
                    material->Get(AI_MATKEY_REFRACTI, Ni);

                    // Convert legacy material to PBR

                    // Base color
                    pbrMat.baseColor = glm::vec4(kd.r, kd.g, kd.b, d);

                    // Approximate metallic factor from specular color
                    float ksLum    = 0.2126f * ks.r + 0.7152f * ks.g + 0.0722f * ks.b;
                    float metallic = 0.0f;
                    if (ksLum >= 0.04f)
                    {
                        metallic = (ksLum - 0.04f) / (1.0f - 0.04f);
                        metallic = glm::clamp(metallic, 0.0f, 1.0f);
                    }
                    pbrMat.metallicFactor = metallic;

                    // Roughness = sqrt(2/(Ns+2))
                    Ns                     = glm::clamp(Ns, 0.0f, 1000.0f);
                    float roughness        = std::sqrt(2.0f / (Ns + 2.0f));
                    pbrMat.roughnessFactor = glm::clamp(roughness, 0.04f, 1.0f);

                    // Emissive color
                    pbrMat.emissiveColorIntensity = glm::vec4(ke.r, ke.g, ke.b, emissiveIntensity);

                    // IOR
                    pbrMat.ior = Ni;

                    // PBR textures & factors

                    // Albedo
                    pbrMat.albedoIndex = loadTexture(material, aiTextureType_DIFFUSE);
                    aiColor4D baseColor;
                    result = material->Get(AI_MATKEY_BASE_COLOR, baseColor);
                    if (result != aiReturn_SUCCESS)
                    {
                        VULTRA_CORE_WARN("[MeshLoader] Failed to get base color");
                    }
                    else
                    {
                        pbrMat.baseColor = glm::vec4(baseColor.r, baseColor.g, baseColor.b, baseColor.a);
                    }

                    // Alpha Mask
                    pbrMat.alphaMaskIndex = loadTexture(material, aiTextureType_OPACITY);
                    if (pbrMat.alphaMaskIndex != 0)
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
                        pbrMat.opacity    = opacity;
                        pbrMat.blendState = (opacity < 1.0f) ? kAlphaBlend : kNoBlend;
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
                        pbrMat.blendState = toBlendState(blendMode);
                    }

                    // Metallic
                    pbrMat.metallicIndex = loadTexture(material, aiTextureType_METALNESS);
                    result               = material->Get(AI_MATKEY_METALLIC_FACTOR, pbrMat.metallicFactor);
                    if (result != aiReturn_SUCCESS)
                    {
                        VULTRA_CORE_WARN("[MeshLoader] Failed to get metallic factor");
                    }

                    // Roughness
                    pbrMat.roughnessIndex = loadTexture(material, aiTextureType_DIFFUSE_ROUGHNESS);
                    result                = material->Get(AI_MATKEY_ROUGHNESS_FACTOR, pbrMat.roughnessFactor);
                    if (result != aiReturn_SUCCESS)
                    {
                        VULTRA_CORE_WARN("[MeshLoader] Failed to get roughness factor");
                    }

                    // Specular
                    pbrMat.specularIndex = loadTexture(material, aiTextureType_SPECULAR);

                    // Normal
                    pbrMat.normalIndex = loadTexture(material, aiTextureType_NORMALS);

                    // Ambient Occlusion
                    pbrMat.aoIndex = loadTexture(material, aiTextureType_LIGHTMAP);

                    // Emissive
                    pbrMat.emissiveIndex = loadTexture(material, aiTextureType_EMISSIVE);

                    // Metallic Roughness
                    pbrMat.metallicRoughnessIndex = loadTexture(material, aiTextureType_GLTF_METALLIC_ROUGHNESS);

                    // Double Sided
                    result = material->Get(AI_MATKEY_TWOSIDED, pbrMat.doubleSided);
                    if (result != aiReturn_SUCCESS)
                    {
                        VULTRA_CORE_WARN("[MeshLoader] Failed to get double sided");
                        pbrMat.doubleSided = true;
                    }

                    mesh.materials[i] = pbrMat;
                }
            };

            std::function<void(const aiNode*, const aiScene*)> processNode;
            processNode = [&processMaterials, &processMesh, &processNode, &vertexOffset, &indexOffset](
                              const aiNode* aiNode, const aiScene* aiScene) {
                processMaterials(aiScene);

                for (uint32_t i = 0; i < aiNode->mNumMeshes; ++i)
                {
                    aiMesh* aiMesh = aiScene->mMeshes[aiNode->mMeshes[i]];
                    processMesh(aiMesh, vertexOffset, indexOffset);
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
    } // namespace gfx
} // namespace vultra