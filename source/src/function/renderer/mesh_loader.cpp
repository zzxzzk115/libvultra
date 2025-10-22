#include "vultra/function/renderer/mesh_loader.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/graphics_pipeline.hpp"
#include "vultra/core/rhi/render_device.hpp"
#include "vultra/core/rhi/util.hpp"
#include "vultra/function/renderer/mesh_manager.hpp"
#include "vultra/function/renderer/texture_manager.hpp"

#include <assimp/GltfMaterial.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <meshoptimizer.h>

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
        struct MatPropValue
        {
            std::variant<aiString, int, float, double, bool, aiColor3D, aiColor4D, std::vector<uint8_t>, aiBlendMode>
                 value;
            bool parsed = false;
        };

        struct MatPropKey
        {
            std::string key;
            unsigned    semantic = 0;
            unsigned    index    = 0;
            bool        operator==(const MatPropKey& other) const noexcept
            {
                return key == other.key && semantic == other.semantic && index == other.index;
            }
        };
        struct MatPropKeyHash
        {
            size_t operator()(const MatPropKey& k) const noexcept
            {
                size_t h1 = std::hash<std::string> {}(k.key);
                size_t h2 = std::hash<unsigned> {}(k.semantic);
                size_t h3 = std::hash<unsigned> {}(k.index);
                return h1 ^ (h2 << 1) ^ (h3 << 2);
            }
        };
        using MatPropMap = std::unordered_map<MatPropKey, MatPropValue, MatPropKeyHash>;

        MatPropMap parseMaterialProperties(const aiMaterial* material)
        {
            MatPropMap result;
            if (!material)
                return result;

            for (unsigned i = 0; i < material->mNumProperties; ++i)
            {
                aiMaterialProperty* prop = material->mProperties[i];
                MatPropKey          key {prop->mKey.C_Str(), prop->mSemantic, prop->mIndex};
                MatPropValue        entry {};

                switch (prop->mType)
                {
                    case aiPTI_String: {
                        aiString value;
                        if (material->Get(prop->mKey.C_Str(), prop->mSemantic, prop->mIndex, value) == aiReturn_SUCCESS)
                            entry.value = std::move(value);
                        break;
                    }
                    case aiPTI_Float: {
                        if (prop->mDataLength / sizeof(float) == 3)
                        {
                            aiColor3D value;
                            if (material->Get(prop->mKey.C_Str(), prop->mSemantic, prop->mIndex, value) ==
                                aiReturn_SUCCESS)
                                entry.value = std::move(value);
                        }
                        else if (prop->mDataLength / sizeof(float) == 4)
                        {
                            aiColor4D value;
                            if (material->Get(prop->mKey.C_Str(), prop->mSemantic, prop->mIndex, value) ==
                                aiReturn_SUCCESS)
                                entry.value = std::move(value);
                        }
                        else
                        {
                            float value;
                            if (material->Get(prop->mKey.C_Str(), prop->mSemantic, prop->mIndex, value) ==
                                aiReturn_SUCCESS)
                                entry.value = std::move(value);
                        }
                        break;
                    }
                    case aiPTI_Double: {
                        double value;
                        if (material->Get(prop->mKey.C_Str(), prop->mSemantic, prop->mIndex, value) == aiReturn_SUCCESS)
                            entry.value = std::move(value);
                        break;
                    }
                    case aiPTI_Integer: {
                        int value;
                        if (material->Get(prop->mKey.C_Str(), prop->mSemantic, prop->mIndex, value) == aiReturn_SUCCESS)
                            entry.value = std::move(value);
                        bool bvalue;
                        if (material->Get(prop->mKey.C_Str(), prop->mSemantic, prop->mIndex, bvalue) ==
                            aiReturn_SUCCESS)
                            entry.value = std::move(bvalue);
                        aiBlendMode bmvalue;
                        if (material->Get(prop->mKey.C_Str(), prop->mSemantic, prop->mIndex, bmvalue) ==
                            aiReturn_SUCCESS)
                            entry.value = std::move(bmvalue);
                        break;
                    }
                    case aiPTI_Buffer: {
                        std::vector<uint8_t> buf(prop->mDataLength);
                        std::memcpy(buf.data(), prop->mData, prop->mDataLength);
                        entry.value = std::move(buf);
                        break;
                    }
                    default: {
                        VULTRA_CORE_WARN("[MeshLoader] Unknown material property type {} for key {}",
                                         static_cast<int>(prop->mType),
                                         prop->mKey.C_Str());
                        break;
                    }
                }
                result.emplace(std::move(key), std::move(entry));
            }
            return result;
        }

        template<typename T>
        bool tryGet(MatPropMap& props, const char* key, unsigned semantic, unsigned index, T& out)
        {
            MatPropKey mk {key, semantic, index};
            auto       it = props.find(mk);
            if (it != props.end())
            {
                it->second.parsed = true;
                if (auto p = std::get_if<T>(&it->second.value))
                {
                    out = *p;
                    return true;
                }
            }
            return false;
        }

        entt::resource_loader<MeshResource>::result_type MeshLoader::operator()(const std::filesystem::path& p,
                                                                                rhi::RenderDevice&           rd)
        {
            if (m_DefaultWhite1x1 == nullptr)
            {
                m_DefaultWhite1x1 = createDefaultTexture(255, 255, 255, 255, rd);
                rd.m_LoadedTextures.push_back(m_DefaultWhite1x1); // Ensure the index 0 is always white 1x1 as fallback
            }

            Assimp::Importer importer;
            const aiScene*   scene =
                importer.ReadFile(p.generic_string(),
                                  aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_PreTransformVertices |
                                      aiProcess_CalcTangentSpace | aiProcess_GenSmoothNormals | aiProcess_GenUVCoords);

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

            const auto processMesh = [this, &mesh, &scene, loadTexture](
                                         const aiMesh* aiMesh, uint32_t& vertexOffset, uint32_t& indexOffset) {
                VULTRA_CORE_TRACE("[MeshLoader] Processing SubMesh: {}, with {} vertices, {} faces, material index {}, "
                                  "material name {}",
                                  aiMesh->mName.C_Str(),
                                  aiMesh->mNumVertices,
                                  aiMesh->mNumFaces,
                                  aiMesh->mMaterialIndex,
                                  aiMesh->mMaterialIndex < scene->mNumMaterials ?
                                      scene->mMaterials[aiMesh->mMaterialIndex]->GetName().C_Str() :
                                      "Unknown");

                // Copy vertices
                std::vector<SimpleVertex> subMeshVertices;
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
                    subMeshVertices.push_back(vertex);
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
                SubMesh subMesh {};
                // subMesh.topology = static_cast<rhi::PrimitiveTopology>(aiMesh->mPrimitiveTypes);
                subMesh.name          = aiMesh->mName.C_Str();
                subMesh.vertexOffset  = vertexOffset;
                subMesh.indexOffset   = indexOffset;
                subMesh.vertexCount   = aiMesh->mNumVertices;
                subMesh.indexCount    = aiMesh->mNumFaces * 3;
                subMesh.materialIndex = aiMesh->mMaterialIndex;

                // Cook AABB
                subMesh.aabb = AABB::build(subMeshVertices);

                mesh.subMeshes.push_back(subMesh);

                vertexOffset += subMesh.vertexCount;
                indexOffset += subMesh.indexCount;
            };

            const auto processMaterials = [this, &mesh, &loadTexture](const aiScene* aiScene) {
                mesh.materials.resize(aiScene->mNumMaterials);

                for (unsigned int i = 0; i < aiScene->mNumMaterials; ++i)
                {
                    const aiMaterial* material = aiScene->mMaterials[i];
                    if (!material)
                        continue;

                    auto        props = parseMaterialProperties(material);
                    PBRMaterial pbrMat {};

                    // Name
                    aiString name;
                    if (tryGet(props, AI_MATKEY_NAME, name))
                    {
                        pbrMat.name = name.C_Str();
                    }

                    // Read color properties
                    aiColor3D kd(1, 1, 1), ks(0, 0, 0), ke(0, 0, 0), ka(0, 0, 0);
                    tryGet(props, AI_MATKEY_COLOR_DIFFUSE, kd);
                    tryGet(props, AI_MATKEY_COLOR_SPECULAR, ks);
                    tryGet(props, AI_MATKEY_COLOR_EMISSIVE, ke);
                    tryGet(props, AI_MATKEY_COLOR_AMBIENT, ka);

                    // Read scalar properties
                    float Ns = 0.0f, d = 1.0f, Ni = 1.5f, emissiveIntensity = 1.0f;
                    tryGet(props, AI_MATKEY_EMISSIVE_INTENSITY, emissiveIntensity);
                    tryGet(props, AI_MATKEY_SHININESS, Ns);
                    tryGet(props, AI_MATKEY_OPACITY, d);
                    tryGet(props, AI_MATKEY_REFRACTI, Ni);

                    // Alpha masking & Blend mode
                    aiString alphaMode;
                    if (tryGet(props, AI_MATKEY_GLTF_ALPHAMODE, alphaMode))
                    {
                        auto alphaModeStr = std::string(alphaMode.C_Str());
                        if (alphaModeStr == "MASK")
                            pbrMat.alphaMode = rhi::AlphaMode::eMask;
                        else if (alphaModeStr == "BLEND")
                            pbrMat.alphaMode = rhi::AlphaMode::eBlend;
                        else
                            pbrMat.alphaMode = rhi::AlphaMode::eOpaque;
                    }
                    float alphaCutoff = 0.5f;
                    if (tryGet(props, AI_MATKEY_GLTF_ALPHACUTOFF, alphaCutoff))
                    {
                        pbrMat.alphaCutoff = alphaCutoff;
                    }
                    aiBlendMode blendMode = aiBlendMode_Default;
                    if (tryGet(props, AI_MATKEY_BLEND_FUNC, blendMode))
                    {
                        pbrMat.blendState = toBlendState(blendMode);
                    }
                    else
                    {
                        pbrMat.blendState = d < 1.0f ? toBlendState(aiBlendMode::aiBlendMode_Default) : kNoBlend;
                    }

                    pbrMat.baseColor = glm::vec4(kd.r, kd.g, kd.b, 1.0);
                    pbrMat.opacity   = d;

                    pbrMat.metallicFactor = std::clamp(
                        (0.2126f * ks.r + 0.7152f * ks.g + 0.0722f * ks.b - 0.04f) / (1.0f - 0.04f), 0.0f, 1.0f);
                    pbrMat.roughnessFactor        = glm::clamp(std::sqrt(2.0f / (Ns + 2.0f)), 0.04f, 1.0f);
                    pbrMat.emissiveColorIntensity = glm::vec4(ke.r, ke.g, ke.b, emissiveIntensity);
                    pbrMat.ior                    = Ni;
                    pbrMat.ambientColor           = glm::vec4(ka.r, ka.g, ka.b, 1.0f);

                    // Override with common PBR extensions if present
                    aiColor4D baseColorFactorPBR(1, 1, 1, 1), emissiveIntensityPBR(0, 0, 0, 1);
                    float     metallicFactorPBR  = 0.0f;
                    float     roughnessFactorPBR = 0.5f;
                    if (tryGet(props, AI_MATKEY_BASE_COLOR, baseColorFactorPBR))
                    {
                        pbrMat.baseColor =
                            glm::vec4(baseColorFactorPBR.r, baseColorFactorPBR.g, baseColorFactorPBR.b, 1.0);
                    }
                    if (tryGet(props, AI_MATKEY_METALLIC_FACTOR, metallicFactorPBR))
                    {
                        pbrMat.metallicFactor = metallicFactorPBR;
                    }
                    if (tryGet(props, AI_MATKEY_ROUGHNESS_FACTOR, roughnessFactorPBR))
                    {
                        pbrMat.roughnessFactor = roughnessFactorPBR;
                    }
                    if (tryGet(props, AI_MATKEY_EMISSIVE_INTENSITY, emissiveIntensityPBR))
                    {
                        pbrMat.emissiveColorIntensity = glm::vec4(emissiveIntensityPBR.r,
                                                                  emissiveIntensityPBR.g,
                                                                  emissiveIntensityPBR.b,
                                                                  emissiveIntensityPBR.a);
                    }

                    // Textures
                    pbrMat.albedoIndex            = loadTexture(material, aiTextureType_DIFFUSE);
                    pbrMat.alphaMaskIndex         = loadTexture(material, aiTextureType_OPACITY);
                    pbrMat.metallicIndex          = loadTexture(material, aiTextureType_METALNESS);
                    pbrMat.roughnessIndex         = loadTexture(material, aiTextureType_DIFFUSE_ROUGHNESS);
                    pbrMat.specularIndex          = loadTexture(material, aiTextureType_SPECULAR);
                    pbrMat.normalIndex            = loadTexture(material, aiTextureType_NORMALS);
                    pbrMat.aoIndex                = loadTexture(material, aiTextureType_LIGHTMAP);
                    pbrMat.emissiveIndex          = loadTexture(material, aiTextureType_EMISSIVE);
                    pbrMat.metallicRoughnessIndex = loadTexture(material, aiTextureType_GLTF_METALLIC_ROUGHNESS);

                    // Double sided?
                    bool doubleSided = false;
                    if (tryGet(props, AI_MATKEY_TWOSIDED, doubleSided))
                    {
                        pbrMat.doubleSided = doubleSided;
                    }

                    // Unhandled properties warning
                    for (auto& [k, v] : props)
                    {
                        if (!v.parsed)
                        {
                            VULTRA_CORE_WARN(
                                "[MeshLoader] Material {} has unhandled property: key='{}' semantic={} index={}",
                                pbrMat.name,
                                k.key,
                                k.semantic,
                                k.index);
                        }
                    }

                    // Log material info
                    VULTRA_CORE_TRACE("[MeshLoader] Loaded Material[{}]: [{}]", i, pbrMat.toString());

                    mesh.materials[i] = pbrMat;
                }
            };

            std::function<void(DefaultMesh&)> generateMeshlets = [](DefaultMesh& outMesh) {
                // --- Build meshlets per submesh ---
                for (auto& sub : outMesh.subMeshes)
                {
                    // Collect positions for meshoptimizer
                    std::vector<glm::vec3> positions;
                    positions.reserve(sub.vertexCount);
                    for (uint32_t v = 0; v < sub.vertexCount; ++v)
                    {
                        const auto& vertex = outMesh.vertices[sub.vertexOffset + v];
                        positions.push_back(vertex.position);
                    }

                    // Extra: meshlets
                    // https://github.com/zeux/meshoptimizer/tree/v0.24#clusterization
                    sub.meshletGroup.meshlets.clear();
                    sub.meshletGroup.meshletTriangles.clear();
                    sub.meshletGroup.meshletVertices.clear();

                    const size_t maxVerts = 64;
                    const size_t maxTris  = 124;

                    const uint32_t* subIndices = outMesh.indices.data() + sub.indexOffset;
                    size_t          subCount   = sub.indexCount;

                    // allocate space for meshlet vertices and triangles
                    size_t                       maxMeshlets = meshopt_buildMeshletsBound(subCount, maxVerts, maxTris);
                    std::vector<meshopt_Meshlet> meshletsTemp(maxMeshlets);

                    // store current offsets
                    size_t baseVertOffset = sub.meshletGroup.meshletVertices.size();
                    size_t baseTriOffset  = sub.meshletGroup.meshletTriangles.size();

                    sub.meshletGroup.meshletVertices.resize(baseVertOffset + maxMeshlets * maxVerts);
                    sub.meshletGroup.meshletTriangles.resize(baseTriOffset + maxMeshlets * maxTris * 3);

                    // build meshlets
                    size_t meshletCount =
                        meshopt_buildMeshlets(meshletsTemp.data(),
                                              sub.meshletGroup.meshletVertices.data() + baseVertOffset,
                                              sub.meshletGroup.meshletTriangles.data() + baseTriOffset,
                                              subIndices,
                                              subCount,
                                              reinterpret_cast<const float*>(positions.data()),
                                              positions.size(),
                                              sizeof(glm::vec3),
                                              maxVerts,
                                              maxTris,
                                              0.5f);

                    meshletsTemp.resize(meshletCount);

                    // copy to Meshlet
                    for (size_t i = 0; i < meshletCount; ++i)
                    {
                        const auto& src = meshletsTemp[i];
                        Meshlet     dst {};

                        dst.vertexOffset   = baseVertOffset + src.vertex_offset;
                        dst.triangleOffset = baseTriOffset + src.triangle_offset;
                        dst.vertexCount    = src.vertex_count;
                        dst.triangleCount  = src.triangle_count;

                        // --- Assign material index directly from submesh ---
                        dst.materialIndex = sub.materialIndex;

                        auto bounds =
                            meshopt_computeMeshletBounds(&sub.meshletGroup.meshletVertices[dst.vertexOffset],
                                                         &sub.meshletGroup.meshletTriangles[dst.triangleOffset],
                                                         dst.triangleCount,
                                                         reinterpret_cast<const float*>(positions.data()),
                                                         positions.size(),
                                                         sizeof(glm::vec3));

                        dst.center     = glm::vec3(bounds.center[0], bounds.center[1], bounds.center[2]);
                        dst.radius     = bounds.radius;
                        dst.coneAxis   = glm::vec3(bounds.cone_axis[0], bounds.cone_axis[1], bounds.cone_axis[2]);
                        dst.coneCutoff = bounds.cone_cutoff;
                        dst.coneApex   = glm::vec3(bounds.cone_apex[0], bounds.cone_apex[1], bounds.cone_apex[2]);

                        sub.meshletGroup.meshlets.push_back(dst);
                    }

                    // Resize to actual used size
                    const auto& last = sub.meshletGroup.meshlets.back();

                    sub.meshletGroup.meshletVertices.resize(last.vertexOffset + last.vertexCount);
                    sub.meshletGroup.meshletTriangles.resize(last.triangleOffset + ((last.triangleCount * 3 + 3) & ~3));
                }
            };

            std::function<void(const aiNode*, const aiScene*)> processNode;
            processNode = [&processMesh, &processNode, &vertexOffset, &indexOffset, &mesh](const aiNode*  aiNode,
                                                                                           const aiScene* aiScene) {
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

            processMaterials(scene);
            processNode(scene->mRootNode, scene);

            // Cook AABB
            mesh.aabb = AABB::build(mesh.vertices);

            // Generate meshlets
            generateMeshlets(mesh);

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

            rd.execute(
                [&](rhi::CommandBuffer& cb) {
                    cb.copyBuffer(
                        stagingVertexBuffer, *mesh.vertexBuffer, vk::BufferCopy {0, 0, stagingVertexBuffer.getSize()});
                    if (stagingIndexBuffer)
                    {
                        cb.copyBuffer(
                            stagingIndexBuffer, *mesh.indexBuffer, vk::BufferCopy {0, 0, stagingIndexBuffer.getSize()});
                    }
                },
                true);

            // Build material buffer (for bindless descriptors)
            mesh.buildMaterialBuffer(rd);

            // Build meshlet buffers for each sub-mesh
            for (auto& sm : mesh.subMeshes)
            {
                sm.buildMeshletBuffers(rd);
            }

            mesh.buildRenderMesh(rd);

            // Find light meshes (meshes with emissive materials)
            for (const auto& sm : mesh.subMeshes)
            {
                if (sm.materialIndex < mesh.materials.size())
                {
                    const auto& mat = mesh.materials[sm.materialIndex];
                    if (mat.emissiveColorIntensity.r > 0.0f || mat.emissiveColorIntensity.g > 0.0f ||
                        mat.emissiveColorIntensity.b > 0.0f || mat.emissiveIndex > 0)
                    {
                        // This sub-mesh is emissive, add its vertices to light mesh
                        std::vector<SimpleVertex> vertices;
                        for (uint32_t vi = 0; vi < sm.vertexCount; ++vi)
                        {
                            const auto& v = mesh.vertices[sm.vertexOffset + vi];
                            vertices.push_back(v);
                        }
                        mesh.lights.push_back(
                            {.vertices = std::move(vertices), .colorIntensity = mat.emissiveColorIntensity});
                        VULTRA_CORE_INFO(
                            "[MeshLoader] Found light mesh in sub-mesh {}, material {}, total light vertices {}",
                            sm.name,
                            mesh.materials[sm.materialIndex].name,
                            mesh.lights.back().vertices.size());
                    }
                }
            }

            return createRef<MeshResource>(std::move(mesh), p);
        }

        entt::resource_loader<MeshResource>::result_type MeshLoader::operator()(const std::string_view name,
                                                                                DefaultMesh&&          mesh) const
        {
            return createRef<MeshResource>(std::move(mesh), name);
        }
    } // namespace gfx
} // namespace vultra