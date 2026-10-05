#include <vultra/core/base/logger.hpp>
#include <vultra/scene/scene_shader_materials.hpp>
#include <vultra/servers/rendering/shader_runtime.hpp>
#include <vultra/servers/rendering/texture_upload.hpp>

#include <algorithm>
#include <map>

namespace vultra
{
    struct SceneShaderMaterials::State
    {
        struct Binding
        {
            ShaderRuntime* runtime;
            uint32_t       index;
            AssetId        shader;
        };

        Device&                device;
        const ProjectManifest& project;
        std::filesystem::path  root;
        VriDescriptor*         sampler = nullptr;
        // Reverse destruction releases GPU materials before their borrowed texture views.
        std::map<std::string, std::unique_ptr<Texture>>       textures;
        std::map<std::string, std::unique_ptr<ShaderRuntime>> shaders;
        std::map<uint64_t, Binding>                           bindings;
        std::map<uint32_t, ShaderMaterial*>                   slots;
        uint64_t                                              appliedMaterials = UINT64_MAX;

        State(Device& device, const ProjectManifest& project, std::filesystem::path root) :
            device(device),
            project(project),
            root(std::filesystem::absolute(root))
        {
        }

        ~State()
        {
            device.waitIdle();
            shaders.clear();
            textures.clear();
            if (sampler)
            {
                device.core.DestroyDescriptor(sampler);
            }
        }

        ShaderTextureBinding texture(const ShaderProperty& property, const ShaderTextureValue& value)
        {
            const auto key   = value.asset.value.toString() + (property.srgb ? ":srgb" : ":linear");
            auto       found = textures.find(key);
            if (found == textures.end())
            {
                const auto path = root / project.asset(value.asset).path;
                Logger::core().info("Importing shader texture {} ({})",
                                    path.string(),
                                    property.srgb ? "sRGB" : "linear");
                // Retain authored DDS mips/compression; color interpretation is selected by the property.
                const auto prepared = loadTextureAsset(path, property.srgb);
                auto       gpu      = uploadTextureAsset(device, prepared);
                found               = textures.emplace(key, std::move(gpu)).first;
                Logger::core().info("Shader texture ready: {}", path.string());
            }
            if (!sampler)
            {
                VriSamplerDesc desc {};
                desc.magFilter    = VriFilter_Linear;
                desc.minFilter    = VriFilter_Linear;
                desc.mipmapMode   = VriMipmapMode_Linear;
                desc.addressModeU = VriAddressMode_Repeat;
                desc.addressModeV = VriAddressMode_Repeat;
                desc.addressModeW = VriAddressMode_Repeat;
                desc.maxLod       = 32;
                check(device.core.CreateSampler(device.handle, &desc, &sampler), "Create scene shader sampler");
            }
            VriTextureViewType dimension = VriTextureViewType_2D;
            switch (found->second->desc.type)
            {
                case VriTextureType_2DArray:
                    dimension = VriTextureViewType_2DArray;
                    break;
                case VriTextureType_3D:
                    dimension = VriTextureViewType_3D;
                    break;
                case VriTextureType_Cube:
                    dimension = VriTextureViewType_Cube;
                    break;
                case VriTextureType_CubeArray:
                    dimension = VriTextureViewType_CubeArray;
                    break;
                default:
                    break;
            }
            return {found->second->view(),
                    sampler,
                    dimension,
                    property.srgb,
                    found->second->desc.format == VriFormat_BC5_UNORM};
        }

        ShaderRuntime& runtime(AssetId shader)
        {
            const auto key   = shader.value.toString();
            auto       found = shaders.find(key);
            if (found == shaders.end())
            {
                ShaderCompileOptions options;
                options.includeDirectories = {std::filesystem::absolute("builtin/shaders"),
                                              std::filesystem::absolute("external"),
                                              root};
                auto runtime               = std::make_unique<ShaderRuntime>(
                    device,
                    root / project.asset(shader).path,
                    options,
                    [this](const ShaderProperty& property, const ShaderTextureValue& value)
                    {
                        return texture(property, value);
                    },
                    std::vector<std::string> {"Forward", "ShadowCaster", "GBufferBase", "GBufferMaterial"},
                    BuiltinRenderer::shaderSubshaderCompatibility);
                found = shaders.emplace(key, std::move(runtime)).first;
            }
            return *found->second;
        }
    };

    SceneShaderMaterials::SceneShaderMaterials(Device&                device,
                                               const ProjectManifest& project,
                                               std::filesystem::path  projectRoot) :
        m_State(std::make_unique<State>(device, project, std::move(projectRoot)))
    {
    }

    SceneShaderMaterials::~SceneShaderMaterials() = default;

    bool SceneShaderMaterials::containsShaders(const SceneTree& tree)
    {
        return std::ranges::any_of(tree.materials(),
                                   [](const auto& material)
                                   {
                                       return material->kind() == MaterialResource::Kind::eShader;
                                   });
    }

    void SceneShaderMaterials::update(const SceneTree&                   tree,
                                      std::span<const SceneMeshInstance> instances,
                                      BuiltinRenderer&                   renderer,
                                      RenderGraph&                       graph,
                                      const BuiltinRenderer::Outputs&    outputs)
    {
        auto& state = *m_State;
        if (state.appliedMaterials != tree.changes().materials)
        {
            for (auto binding = state.bindings.begin(); binding != state.bindings.end();)
            {
                const auto* resource = tree.findMaterial(ObjectId {binding->first});
                if (!resource || resource->kind() != MaterialResource::Kind::eShader ||
                    resource->shaderMaterial().shader != binding->second.shader)
                {
                    auto* old = &binding->second.runtime->material(binding->second.index);
                    std::erase_if(state.slots,
                                  [&](const auto& slot)
                                  {
                                      if (slot.second != old)
                                      {
                                          return false;
                                      }
                                      renderer.setShaderMaterial(slot.first, nullptr);
                                      return true;
                                  });
                    binding->second.runtime->removeMaterial(binding->second.index);
                    binding = state.bindings.erase(binding);
                }
                else
                {
                    ++binding;
                }
            }
            for (const auto& resource : tree.materials())
            {
                if (resource->kind() != MaterialResource::Kind::eShader)
                {
                    continue;
                }
                const auto& instance = resource->shaderMaterial();
                auto        found    = state.bindings.find(resource->id().value);
                if (found == state.bindings.end() || found->second.shader != instance.shader)
                {
                    auto& runtime = state.runtime(instance.shader);
                    found =
                        state.bindings
                            .insert_or_assign(resource->id().value,
                                              State::Binding {&runtime, runtime.addMaterial(instance), instance.shader})
                            .first;
                }
                found->second.runtime->instance(found->second.index).assign(found->second.runtime->asset(), instance);
            }
            state.appliedMaterials = tree.changes().materials;
        }
        for (auto& [id, runtime] : state.shaders)
        {
            const bool published = runtime->poll(
                [&](uint32_t, ShaderMaterial& candidate)
                {
                    renderer.prepareShaderMaterial(candidate, graph, outputs);
                });
            if (published)
            {
                for (const auto& [object, binding] : state.bindings)
                {
                    auto* resource = tree.findMaterial(ObjectId {object});
                    if (binding.runtime == runtime.get() && resource &&
                        resource->kind() == MaterialResource::Kind::eShader &&
                        resource->shaderMaterial().shader == binding.shader)
                    {
                        resource->setShaderMaterial(runtime->instance(binding.index));
                    }
                }
            }
        }
        std::map<uint32_t, ShaderMaterial*> slots;
        for (const auto& instance : instances)
        {
            const auto& mesh = static_cast<const MeshInstanceNode&>(*tree.find(instance.node));
            for (const auto& override : mesh.materialOverrides())
            {
                const auto& resource = *tree.findMaterial(override.material);
                if (resource.kind() == MaterialResource::Kind::eShader)
                {
                    const auto& binding = state.bindings.at(resource.id().value);
                    slots.emplace(instance.firstMaterial + override.slot, &binding.runtime->material(binding.index));
                }
            }
        }
        for (const auto& [slot, material] : state.slots)
        {
            if (!slots.contains(slot))
            {
                renderer.setShaderMaterial(slot, nullptr);
            }
        }
        for (const auto& [slot, material] : slots)
        {
            const auto previous = state.slots.find(slot);
            if (previous == state.slots.end() || previous->second != material)
            {
                renderer.setShaderMaterial(slot, material);
            }
        }
        state.slots = std::move(slots);
    }

    const ShaderAsset& SceneShaderMaterials::asset(const MaterialInstance& material)
    {
        return m_State->runtime(material.shader).asset();
    }

    std::string SceneShaderMaterials::diagnostics() const
    {
        std::string result;
        for (const auto& [id, shader] : m_State->shaders)
        {
            result += shader->diagnostics();
        }
        return result;
    }
} // namespace vultra
