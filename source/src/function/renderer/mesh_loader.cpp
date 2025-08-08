#include "vultra/function/renderer/mesh_loader.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/util.hpp"
#include "vultra/function/renderer/texture_manager.hpp"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

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

            if (m_DefaultBlack1x1 == nullptr)
            {
                m_DefaultBlack1x1 = createDefaultTexture(0, 0, 0, 255, rd);
            }

            if (m_DefaultNormal1x1 == nullptr)
            {
                m_DefaultNormal1x1 = createDefaultTexture(127, 127, 255, 255, rd);
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
                    return resource::loadResource<TextureManager>((p.parent_path() / path.data).generic_string());
                }
                else if (type == aiTextureType_EMISSIVE || type == aiTextureType_EMISSION_COLOR)
                {
                    return m_DefaultBlack1x1;
                }
                else if (type == aiTextureType_NORMALS)
                {
                    return m_DefaultNormal1x1;
                }

                return m_DefaultWhite1x1;
            };

            const auto processMesh = [&mesh, loadTexture](const aiMesh*  aiMesh,
                                                          const aiScene* aiScene,
                                                          uint32_t&      vertexOffset,
                                                          uint32_t&      indexOffset) {
                // Copy vertices
                for (unsigned int v = 0; v < aiMesh->mNumVertices; ++v)
                {
                    SimpleVertex vertex {};
                    vertex.position = {aiMesh->mVertices[v].x, aiMesh->mVertices[v].y, aiMesh->mVertices[v].z};

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
                    const aiMaterial* material         = aiScene->mMaterials[aiMesh->mMaterialIndex];
                    subMesh.material.albedo            = loadTexture(material, aiTextureType_DIFFUSE);
                    subMesh.material.metallicRoughness = loadTexture(material, aiTextureType_METALNESS);
                    subMesh.material.normal            = loadTexture(material, aiTextureType_NORMALS);
                    subMesh.material.ao                = loadTexture(material, aiTextureType_AMBIENT_OCCLUSION);
                    subMesh.material.emissive          = loadTexture(material, aiTextureType_EMISSIVE);
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
                    stagingVertexBuffer, *mesh.vertexBuffer, vk::BufferCopy {0, 0, mesh.vertexBuffer->getSize()});
                if (stagingIndexBuffer)
                {
                    cb.copyBuffer(
                        stagingIndexBuffer, *mesh.indexBuffer, vk::BufferCopy {0, 0, mesh.indexBuffer->getSize()});
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