// AssetSystem material upload / refresh translation unit.
//
// Split out of asset_system.cpp: turning a
// vasset::VMaterial into a GpuMaterial entry, refreshing GPU material params once their
// textures become resident, and (editor only) emitting imported material assets. These share
// the GPU material-param packers and the material JSON (de)serialization helpers, so they
// move together. The imported-material relative-path helper is shared with the registry's
// import scan and lives in imported_material_path.hpp.

#include "vultra/function/asset/asset_system.hpp"
#include "vultra/core/base/common_context.hpp"
#include "vultra/core/builtin/builtin_resources.hpp"
#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/core/rhi/structs/render_mesh.hpp"
#include "vultra/core/rhi/structs/vertex_attributes.hpp"
#include "vultra/function/asset/asset_memory_estimate.hpp"
#include "vultra/function/asset/builtin_assets.hpp"
#include "vultra/function/asset/builtin_assets_io.hpp"
#include "vultra/function/asset/builtin_resource_ids.hpp"
#include "vultra/function/asset/mesh_vertex_packing.hpp"
#include "vultra/function/material/material_asset.hpp"
#include "vultra/function/material/material_params.hpp"
#include "vultra/function/rendering/srp/builtin/builtin_rendergraph_registry.hpp"
#include "vultra/function/resource/vtexture_loader.hpp"
#include "vultra/function/services/render_backend_service.hpp"

#ifdef VULTRA_HAS_VASSET_IMPORT
#include "vultra/core/builtin/builtin_resources.hpp"

#include <vasset/editor_filesystem.hpp>
#include <vasset/vasset_importers.hpp>
#endif
#include <vasset/vanimation.hpp>
#include <vasset/vgaussiansplat.hpp>
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
#include "vultra/function/asset/imported_material_path.hpp"

namespace vultra
{
    using asset_detail::importedMaterialAssetRelativePath;

    namespace
    {
        using namespace resource;
        using namespace rhi;
        using namespace vasset;

        constexpr std::size_t kMaxMaterialRefreshChecksPerFrame = 8;

        nlohmann::json jsonVec4(const glm::vec4& v) { return nlohmann::json::array({v.x, v.y, v.z, v.w}); }

        nlohmann::json jsonVec3(const glm::vec3& v) { return nlohmann::json::array({v.x, v.y, v.z}); }

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

        enum class PbrMrTextureMode : uint32_t
        {
            eGltfMetallicRoughness      = 0,
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

        // GPU material-param packers shared by createAndAppendGpuMaterial (alloc-new),
        // refreshGpuMaterialParams (upload-in-place), and createAndAppendGpuMaterialFromAsset
        // (fallback seed, with the emissive fixup disabled, before layering JSON overrides).
        // `resolveTex` maps a texture UUID to its bindless index (resolveBindlessTextureIndexAsync
        // at all call sites).
        template<typename ResolveTex>
        MaterialParamsPBRMR packMaterialParamsPBRMR(const vasset::VMaterial& m,
                                                    ResolveTex&&             resolveTex,
                                                    const bool               emissiveBlackToWhiteFixup = true)
        {
            MaterialParamsPBRMR p;
            p.baseColor       = m.core.pbrMR.baseColor;
            p.metallicFactor  = m.core.pbrMR.metallicFactor;
            p.roughnessFactor = m.core.pbrMR.roughnessFactor;
            p.alphaCutoff     = m.core.pbrMR.alphaCutoff;
            p.alphaMode       = static_cast<uint32_t>(m.core.pbrMR.alphaMode);
            p.baseColorTex    = resolveTex(CoreUUID(m.core.pbrMR.baseColorTexture.uuid));
            p.normalTex       = resolveTex(CoreUUID(m.core.pbrMR.normalTexture.uuid));
            p.mrTex           = resolveTex(pbrMrCombinedTextureUuid(m.core.pbrMR));
            p.metallicTex     = resolveTex(CoreUUID(m.core.pbrMR.metallicTexture.uuid));
            p.roughnessTex    = resolveTex(CoreUUID(m.core.pbrMR.roughnessTexture.uuid));
            p.occlusionTex    = resolveTex(CoreUUID(m.core.pbrMR.ambientOcclusionTexture.uuid));
            p.emissiveTex     = resolveTex(CoreUUID(m.core.pbrMR.emissiveTexture.uuid));
            {
                glm::vec3 emissiveColor = glm::vec3(m.core.pbrMR.emissiveColorIntensity);
                // Texture present but factor came through black (assimp glTF quirk): emissive
                // = factor * texture would be zero, so default the factor to white. Material
                // assets opt out (their factor is authored, not imported).
                if (emissiveBlackToWhiteFixup && p.emissiveTex != 0u && emissiveColor == glm::vec3(0.0f))
                    emissiveColor = glm::vec3(1.0f);
                p.emissiveFactor = glm::vec4(emissiveColor * m.core.pbrMR.emissiveColorIntensity.a, 1.0f);
            }
            p.doubleSided   = m.core.pbrMR.doubleSided ? 1u : 0u;
            p.mrTextureMode = static_cast<uint32_t>(pbrMrTextureMode(m.core.pbrMR));
            return p;
        }

        template<typename ResolveTex>
        MaterialParamsPBRSG packMaterialParamsPBRSG(const vasset::VMaterial& m, ResolveTex&& resolveTex)
        {
            MaterialParamsPBRSG p;
            p.diffuseColor          = m.core.pbrSG.diffuseColor;
            p.specularFactor        = m.core.pbrSG.specularFactor;
            p.glossinessFactor      = m.core.pbrSG.glossinessFactor;
            p.diffuseColorTex       = resolveTex(CoreUUID(m.core.pbrSG.diffuseTexture.uuid));
            p.specularGlossinessTex = resolveTex(CoreUUID(m.core.pbrSG.specularGlossinessTexture.uuid));
            p.glossinessTex         = resolveTex(CoreUUID(m.core.pbrSG.glossinessTexture.uuid));
            p.normalTex             = resolveTex(CoreUUID(m.core.pbrSG.normalTexture.uuid));
            return p;
        }

        template<typename ResolveTex>
        MaterialParamsUnlit packMaterialParamsUnlit(const vasset::VMaterial& m, ResolveTex&& resolveTex)
        {
            MaterialParamsUnlit p;
            p.color    = m.core.unlit.color;
            p.colorTex = resolveTex(CoreUUID(m.core.unlit.colorTexture.uuid));
            return p;
        }

        template<typename ResolveTex>
        MaterialParamsPhong packMaterialParamsPhong(const vasset::VMaterial& m, ResolveTex&& resolveTex)
        {
            MaterialParamsPhong p;
            p.diffuse           = m.core.phong.diffuse;
            p.specularShininess = glm::vec4(m.core.phong.specular, m.core.phong.shininess);
            p.diffuseTex        = resolveTex(CoreUUID(m.core.phong.diffuseTexture.uuid));
            return p;
        }
    } // namespace

    bool AssetSystem::materialTextureDependenciesReady(const vasset::VMaterial& material)
    {
        // PASSIVE residency check -- must not trigger a load here. This runs inside
        // update() -> refreshPendingMaterialParams(); calling loadTextureAsync() (which under sync
        // loading is loadTextureSync -> loadGpuAssetSync -> update()) re-enters update() recursively
        // and starves the upload-drain that would actually make the texture resident, so the material
        // could stay stuck on the fallback (white/flat) texture indefinitely. The textures are loaded
        // by the material's resolveTex (packMaterialParams*); here we only observe GPU residency.
        auto ready = [this](const CoreUUID& uuid) {
            if (!uuid.valid())
                return true;
            auto* rec = m_TextureCache.findOrCreate(uuid);
            return rec != nullptr && rec->state.load(std::memory_order_acquire) == AssetState::eReady &&
                   rec->gpuIndex.load(std::memory_order_acquire) != std::numeric_limits<uint32_t>::max();
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
                // Wait for EVERY texture packMaterialParamsPBRSG binds. Mixamo SG exports a separate
                // glossiness map + normal map; omitting them let the material be re-packed and dropped
                // from the refresh queue while those were still the white/flat fallback (wrong "反光").
                return ready(CoreUUID(material.core.pbrSG.diffuseTexture.uuid)) &&
                       ready(CoreUUID(material.core.pbrSG.specularGlossinessTexture.uuid)) &&
                       ready(CoreUUID(material.core.pbrSG.glossinessTexture.uuid)) &&
                       ready(CoreUUID(material.core.pbrSG.normalTexture.uuid));
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

        const auto resolveTex  = [this](const CoreUUID& uuid) { return resolveBindlessTextureIndexAsync(uuid); };
        auto&      gpuMaterial = pool.materials[materialIndex];
        switch (material.model)
        {
            case vasset::VMaterialModel::ePBRMetallicRoughness: {
                const auto p = packMaterialParamsPBRMR(material, resolveTex);
                uploadBlock(gpuMaterial, &p, sizeof(p));
                break;
            }
            case vasset::VMaterialModel::ePBRSpecularGlossiness: {
                const auto p = packMaterialParamsPBRSG(material, resolveTex);
                uploadBlock(gpuMaterial, &p, sizeof(p));
                break;
            }
            case vasset::VMaterialModel::eUnlit: {
                const auto p = packMaterialParamsUnlit(material, resolveTex);
                uploadBlock(gpuMaterial, &p, sizeof(p));
                break;
            }
            case vasset::VMaterialModel::ePhong:
            default: {
                const auto p = packMaterialParamsPhong(material, resolveTex);
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
        const auto resolveTex = [this](const CoreUUID& uuid) { return resolveBindlessTextureIndexAsync(uuid); };

        switch (m.model)
        {
            case vasset::VMaterialModel::ePBRMetallicRoughness: {
                gm.model     = GpuMaterialModel::ePBRMetallicRoughness;
                const auto p = packMaterialParamsPBRMR(m, resolveTex);
                blockOffset  = allocBlock(&p, sizeof(p));
                break;
            }
            case vasset::VMaterialModel::ePBRSpecularGlossiness: {
                gm.model     = GpuMaterialModel::ePBRSpecularGlossiness;
                const auto p = packMaterialParamsPBRSG(m, resolveTex);
                blockOffset  = allocBlock(&p, sizeof(p));
                break;
            }
            case vasset::VMaterialModel::eUnlit: {
                gm.model     = GpuMaterialModel::eUnlit;
                const auto p = packMaterialParamsUnlit(m, resolveTex);
                blockOffset  = allocBlock(&p, sizeof(p));
                break;
            }
            case vasset::VMaterialModel::ePhong:
            default: {
                gm.model     = GpuMaterialModel::ePhong;
                const auto p = packMaterialParamsPhong(m, resolveTex);
                blockOffset  = allocBlock(&p, sizeof(p));
                break;
            }
        }

        gm.blockOffsetBytes = blockOffset;
        gm.tableIndex       = static_cast<uint32_t>(pool.materials.size());
        pool.materials.push_back(gm);
        if (!materialTextureDependenciesReady(m))
            m_PendingMaterialRefreshes.push_back(
                PendingMaterialRefresh {.materialIndex = gm.tableIndex, .material = m});
        pool.materialTableDirty = true;
        m_GpuResourceService->markContentDirty();
        return gm.tableIndex;
    }

    uint32_t AssetSystem::createAndAppendGpuMaterialFromAsset(const std::string_view   uri,
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
            // Seed from the fallback with the emissive black->white import fixup disabled
            // (a material asset's factor is authored); JSON overrides layer on top below.
            p = packMaterialParamsPBRMR(
                fallback,
                [this](const CoreUUID& uuid) { return resolveBindlessTextureIndexAsync(uuid); },
                /*emissiveBlackToWhiteFixup=*/false);
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

        const auto* properties =
            doc.contains("properties") && doc["properties"].is_object() ? &doc["properties"] : nullptr;
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

            p.baseColorTex  = textureIndexForUri(*properties, "baseColorTexture");
            p.normalTex     = textureIndexForUri(*properties, "normalTexture");
            p.mrTex         = textureIndexForUri(*properties, "metallicRoughnessTexture");
            p.metallicTex   = textureIndexForUri(*properties, "metallicTexture");
            p.roughnessTex  = textureIndexForUri(*properties, "roughnessTexture");
            p.occlusionTex  = textureIndexForUri(*properties, "ambientOcclusionTexture");
            p.emissiveTex   = textureIndexForUri(*properties, "emissiveTexture");
            p.mrTextureMode = p.mrTex != 0u ? static_cast<uint32_t>(PbrMrTextureMode::eGltfMetallicRoughness) : 0u;
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

            const auto name = material.name.empty() ? std::string("Material ") + std::to_string(slot) : material.name;
            return nlohmann::json {
                {"type", "Material"},
                {"version", 1},
                {"name", name},
                {"source", {{"kind", "builtin"}, {"id", "builtin/pbr"}}},
                {"properties", properties},
            };
        };

        const auto source           = std::string(sourceRelativePath);
        const auto sourceMeshPrefix = source + "#mesh/";
        for (const auto& [uuidText, entry] : m_Registry.getRegistry())
        {
            static_cast<void>(uuidText);
            if (entry.type != vasset::VAssetType::eMesh || entry.importedPath.empty())
                continue;
            if (entry.sourcePath != source && !entry.sourcePath.starts_with(sourceMeshPrefix))
                continue;

            vasset::VMesh mesh;
            const auto    meshPath = (std::filesystem::path(m_Desc.assetRoot) / entry.importedPath).lexically_normal();
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
} // namespace vultra
