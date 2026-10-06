#include "shader_pass_state.hpp"

#include <vultra/core/base/logger.hpp>

#include <algorithm>
#include <cstring>
#include <map>
#include <stdexcept>

namespace vultra
{
    namespace
    {
        VriTextureViewType dimension(uint32_t shape)
        {
            switch (shape)
            {
                case 0x42:
                    return VriTextureViewType_2DArray;
                case 3:
                    return VriTextureViewType_3D;
                case 4:
                    return VriTextureViewType_Cube;
                case 0x44:
                    return VriTextureViewType_CubeArray;
                default:
                    return VriTextureViewType_2D;
            }
        }

        VriTextureViewType dimension(ShaderPropertyType type)
        {
            switch (type)
            {
                case ShaderPropertyType::eTexture2DArray:
                    return VriTextureViewType_2DArray;
                case ShaderPropertyType::eTexture3D:
                    return VriTextureViewType_3D;
                case ShaderPropertyType::eTextureCube:
                    return VriTextureViewType_Cube;
                case ShaderPropertyType::eTextureCubeArray:
                    return VriTextureViewType_CubeArray;
                default:
                    return VriTextureViewType_2D;
            }
        }
    } // namespace

    ShaderPassContext::ShaderPassContext(const ShaderPassContext& other) :
        colors(other.colors),
        depth(other.depth),
        attributes(other.attributes),
        streams(other.streams),
        topology(other.topology),
        samples(other.samples),
        viewMask(other.viewMask),
        mirrored(other.mirrored),
        depthReadOnly(other.depthReadOnly)
    {
        m_SemanticNames.resize(attributes.size());
        for (size_t i = 0; i < attributes.size(); ++i)
        {
            if (attributes[i].semanticName)
            {
                m_SemanticNames[i]         = attributes[i].semanticName;
                attributes[i].semanticName = m_SemanticNames[i].c_str();
            }
        }
    }

    ShaderPassContext& ShaderPassContext::operator=(const ShaderPassContext& other)
    {
        if (this != &other)
        {
            *this = ShaderPassContext(other);
        }
        return *this;
    }

    struct ShaderMaterial::State
    {
        struct DefaultTexture
        {
            Device&                  device;
            std::unique_ptr<Texture> texture;
            VriDescriptor*           view = nullptr;

            explicit DefaultTexture(Device& device) :
                device(device)
            {
            }

            ~DefaultTexture()
            {
                if (view)
                {
                    device.core.DestroyDescriptor(view);
                }
            }
        };

        struct Pass
        {
            Device&                                                               device;
            const ShaderPass&                                                     definition;
            const ShaderProgram&                                                  program;
            VriPipelineLayout*                                                    layout = nullptr;
            VriDescriptorPool*                                                    pool   = nullptr;
            std::vector<VriDescriptorSet*>                                        sets;
            std::vector<uint32_t>                                                 spaces;
            std::vector<ShaderResourceBinding>                                    bindings;
            std::map<std::string, std::pair<uint32_t, uint32_t>, std::less<>>     ranges;
            std::unique_ptr<Buffer>                                               uniforms;
            VriDescriptor*                                                        uniformView = nullptr;
            uint64_t                                                              revision    = UINT64_MAX;
            uint32_t                                                              pushSize    = 0;
            std::map<std::string, bool>                                           normalEncodings;
            std::map<std::string, std::vector<const VriDescriptor*>, std::less<>> bound;

            Pass(Device& device, const ShaderPass& definition, const ShaderProgram& program) :
                device(device),
                definition(definition),
                program(program),
                bindings(program.resourceBindings())
            {
            }

            ~Pass()
            {
                if (pool)
                {
                    device.core.DestroyDescriptorPool(pool);
                }
                if (uniformView)
                {
                    device.core.DestroyDescriptor(uniformView);
                }
                if (layout)
                {
                    device.core.DestroyPipelineLayout(layout);
                }
            }
        };

        struct Pipeline
        {
            VriPipeline* handle = nullptr;
            Pass*        pass   = nullptr;
            PreparedPass prepared;
        };

        Device&                                                device;
        const ShaderAsset&                                     asset;
        MaterialInstance&                                      instance;
        TextureResolver                                        resolveTexture;
        uint32_t                                               selected       = 0;
        VriDescriptor*                                         defaultSampler = nullptr;
        std::map<std::string, std::unique_ptr<DefaultTexture>> defaults;
        std::map<std::string, std::unique_ptr<Pass>>           passes;
        std::map<std::string, Pipeline>                        pipelines;
        std::map<std::string, Pipeline*, std::less<>>          active;

        State(Device&                                    device,
              const ShaderAsset&                         asset,
              MaterialInstance&                          instance,
              TextureResolver                            resolver,
              std::span<const std::string_view>          requiredLightModes,
              const ShaderAsset::SubshaderCompatibility& compatible) :
            device(device),
            asset(asset),
            instance(instance),
            resolveTexture(std::move(resolver))
        {
            std::string diagnostics;
            selected = asset.selectSubshader("Vultra", device.features, requiredLightModes, diagnostics, compatible);
            Logger::core().info("Shader {} selected SubShader {}\n{}", asset.name, selected, diagnostics);
            instance.validate(asset);
        }

        ~State()
        {
            device.waitIdle();
            for (auto& [key, pipeline] : pipelines)
            {
                device.core.DestroyPipeline(pipeline.handle);
            }
            if (defaultSampler)
            {
                device.core.DestroyDescriptor(defaultSampler);
            }
        }

        ShaderTextureBinding texture(const ShaderProperty& property, const ShaderTextureValue& value)
        {
            if (value.builtin.empty())
            {
                if (!resolveTexture)
                {
                    throw std::invalid_argument("No texture resolver for property: " + property.name);
                }
                auto result = resolveTexture(property, value);
                if (!result.texture || !result.sampler || result.dimension != dimension(property.type))
                {
                    throw std::invalid_argument("Texture resource/dimension mismatch: " + property.name);
                }
                if (result.srgb != property.srgb)
                {
                    throw std::invalid_argument("Texture color-space/normal usage mismatch: " + property.name);
                }
                return result;
            }
            if (!defaultSampler)
            {
                VriSamplerDesc sampler {};
                sampler.minFilter    = VriFilter_Linear;
                sampler.magFilter    = VriFilter_Linear;
                sampler.mipmapMode   = VriMipmapMode_Linear;
                sampler.maxLod       = 1;
                sampler.addressModeU = VriAddressMode_Repeat;
                sampler.addressModeV = VriAddressMode_Repeat;
                sampler.addressModeW = VriAddressMode_Repeat;
                check(device.core.CreateSampler(device.handle, &sampler, &defaultSampler),
                      "Create shader default sampler");
            }
            const auto key   = value.builtin + std::to_string(uint32_t(property.type));
            auto       found = defaults.find(key);
            if (found == defaults.end())
            {
                auto desc           = colorTexture({1, 1}, VriFormat_RGBA32_SFLOAT);
                desc.usage          = VriTextureUsage_ShaderResource | VriTextureUsage_TransferDst;
                const auto viewType = dimension(property.type);
                switch (viewType)
                {
                    case VriTextureViewType_2DArray:
                        desc.type = VriTextureType_2DArray;
                        break;
                    case VriTextureViewType_3D:
                        desc.type = VriTextureType_3D;
                        break;
                    case VriTextureViewType_Cube:
                        desc.type = VriTextureType_Cube;
                        break;
                    case VriTextureViewType_CubeArray:
                        desc.type = VriTextureType_CubeArray;
                        break;
                    default:
                        desc.type = VriTextureType_2D;
                        break;
                }
                desc.layerNum =
                    (viewType == VriTextureViewType_Cube || viewType == VriTextureViewType_CubeArray) ? 6 : 1;
                auto created     = std::make_unique<DefaultTexture>(device);
                created->texture = std::make_unique<Texture>(device, desc);
                Buffer upload(
                    device,
                    {uint64_t(desc.layerNum) * 512, 0, VriBufferUsage_TransferSrc, VriMemoryLocation_HostUpload});
                auto* mapped = static_cast<std::byte*>(device.core.MapBuffer(upload.handle, 0, upload.desc.size));
                if (!mapped)
                {
                    throw std::runtime_error("Map default shader texture");
                }
                std::array<float, 4> color {1, 1, 1, 1};
                if (value.builtin == "black")
                {
                    color = {0, 0, 0, 1};
                }
                else if (value.builtin == "normal")
                {
                    color = {0.5f, 0.5f, 1, 1};
                }
                for (uint32_t layer = 0; layer < desc.layerNum; ++layer)
                {
                    std::memcpy(mapped + uint64_t(layer) * 512, color.data(), sizeof(color));
                }
                device.core.UnmapBuffer(upload.handle);
                Frame frame(device);
                auto* cmd = frame.begin();
                created->texture->transition(
                    cmd,
                    {VriAccess_CopyDestinationWrite, VriLayout_CopyDestination, VriPipelineStage_Transfer});
                for (uint32_t layer = 0; layer < desc.layerNum; ++layer)
                {
                    VriBufferTextureCopyDesc copy {};
                    copy.bufferOffset      = uint64_t(layer) * 512;
                    copy.bufferRowLength   = 16;
                    copy.bufferImageHeight = 1;
                    copy.texture           = {0, layer, 1, VriImageAspect_Color, 0, 0, 0, 1, 1, 1};
                    device.core.CmdUploadBufferToTexture(cmd, created->texture->handle, upload.handle, &copy);
                }
                created->texture->transition(
                    cmd,
                    {VriAccess_ShaderResourceRead, VriLayout_ShaderResource, VriPipelineStage_AllCommands});
                frame.submitAndWait();
                VriTextureViewDesc view {};
                view.texture  = created->texture->handle;
                view.viewType = viewType;
                view.format   = desc.format;
                view.aspect   = VriImageAspect_Color;
                view.layerNum = desc.layerNum;
                check(device.core.CreateTextureView(device.handle, &view, &created->view),
                      "Create default shader texture view");
                found = defaults.emplace(key, std::move(created)).first;
            }
            return {found->second->view, defaultSampler, dimension(property.type), false, false};
        }

        Pass& pass(const ShaderPass& definition)
        {
            const auto key   = definition.name + ":" + instance.variant;
            const auto found = passes.find(key);
            if (found != passes.end())
            {
                return *found->second;
            }
            auto candidate = std::make_unique<Pass>(device, definition, definition.program(instance.variant));
            std::map<uint32_t, std::vector<VriDescriptorRangeDesc>> groups;
            VriShaderStageFlags                                     stages = 0;
            for (const auto& entry : definition.entries)
            {
                stages |= entry.stage;
            }
            std::vector<VriPushConstantDesc> pushes;
            VriDescriptorPoolDesc            pool {};
            for (const auto& binding : candidate->bindings)
            {
                if (binding.pushConstant)
                {
                    if (binding.uniformSize > 128 || !pushes.empty())
                    {
                        throw std::invalid_argument("Material Pass supports one push constant block up to 128 bytes");
                    }
                    candidate->pushSize = uint32_t(binding.uniformSize);
                    pushes.push_back({0, candidate->pushSize, stages});
                    continue;
                }
                auto& ranges = groups[binding.set];
                if (std::ranges::any_of(ranges,
                                        [&](const auto& range)
                                        {
                                            return range.baseRegister == binding.binding;
                                        }))
                {
                    throw std::invalid_argument("Overlapping reflected shader bindings: " + binding.name);
                }
                candidate->ranges.emplace(binding.name, std::pair(binding.set, uint32_t(ranges.size())));
                ranges.push_back({binding.binding, binding.count, binding.type, stages, 0, dimension(binding.shape)});
                switch (binding.type)
                {
                    case VriDescriptorType_Sampler:
                        pool.samplerMaxNum += binding.count;
                        break;
                    case VriDescriptorType_Texture:
                        pool.textureMaxNum += binding.count;
                        break;
                    case VriDescriptorType_StorageTexture:
                        pool.storageTextureMaxNum += binding.count;
                        break;
                    case VriDescriptorType_ConstantBuffer:
                        pool.constantBufferMaxNum += binding.count;
                        break;
                    case VriDescriptorType_StructuredBuffer:
                        pool.structuredBufferMaxNum += binding.count;
                        break;
                    case VriDescriptorType_StorageBuffer:
                        pool.storageBufferMaxNum += binding.count;
                        break;
                    case VriDescriptorType_AccelerationStructure:
                        pool.accelerationStructureMaxNum += binding.count;
                        break;
                    default:
                        throw std::invalid_argument("Unsupported shader descriptor type");
                }
                if (binding.name == "material")
                {
                    candidate->uniforms = std::make_unique<Buffer>(device,
                                                                   VriBufferDesc {binding.uniformSize,
                                                                                  0,
                                                                                  VriBufferUsage_ConstantBuffer,
                                                                                  VriMemoryLocation_HostUpload});
                    VriBufferViewDesc view {candidate->uniforms->handle,
                                            VriDescriptorType_ConstantBuffer,
                                            VriFormat_Unknown,
                                            0,
                                            binding.uniformSize};
                    check(device.core.CreateBufferView(device.handle, &view, &candidate->uniformView),
                          "Create shader material uniform view");
                }
            }
            std::vector<VriDescriptorSetDesc> sets;
            if (!groups.empty())
            {
                const auto highestSet = groups.rbegin()->first;
                if (highestSet >= 32)
                {
                    throw std::invalid_argument("Reflected descriptor set index exceeds material layout limit (31)");
                }
                // Vulkan set indices are positional; registerSpace does not create skipped layouts.
                for (uint32_t space = 0; space <= highestSet; ++space)
                {
                    const auto& ranges = groups[space];
                    candidate->spaces.push_back(space);
                    sets.push_back({space, ranges.data(), uint32_t(ranges.size())});
                }
            }
            VriPipelineLayoutDesc layout {sets.data(),
                                          uint32_t(sets.size()),
                                          pushes.data(),
                                          uint32_t(pushes.size()),
                                          stages};
            check(device.core.CreatePipelineLayout(device.handle, &layout, &candidate->layout),
                  "Create reflected shader layout");
            if (!sets.empty())
            {
                pool.descriptorSetMaxNum = uint32_t(sets.size());
                check(device.core.CreateDescriptorPool(device.handle, &pool, &candidate->pool),
                      "Create shader material descriptor pool");
                candidate->sets.resize(sets.size());
                for (uint32_t index = 0; index < sets.size(); ++index)
                {
                    check(device.core.AllocateDescriptorSets(candidate->pool,
                                                             candidate->layout,
                                                             index,
                                                             &candidate->sets[index],
                                                             1),
                          "Allocate reflected shader descriptor set");
                }
            }
            return *passes.emplace(key, std::move(candidate)).first->second;
        }

        void update(Pass& pass, std::span<const ShaderResourceViews> resources)
        {
            std::map<std::string, ShaderTextureBinding> textures;
            for (const auto& property : asset.properties)
            {
                if (property.isTexture())
                {
                    textures.emplace(
                        property.name,
                        texture(property, std::get<ShaderTextureValue>(instance.value(asset, property.name))));
                }
            }
            std::map<std::string, std::vector<const VriDescriptor*>> resolved;
            for (const auto& binding : pass.bindings)
            {
                if (binding.pushConstant)
                {
                    continue;
                }
                std::vector<const VriDescriptor*> views;
                if (binding.name == "material")
                {
                    views.push_back(pass.uniformView);
                }
                else if (binding.name.starts_with("material."))
                {
                    auto       name    = binding.name.substr(9);
                    const bool sampler = binding.type == VriDescriptorType_Sampler;
                    if (sampler && name.ends_with("Sampler"))
                    {
                        name.resize(name.size() - 7);
                    }
                    const auto found = textures.find(name);
                    if (found == textures.end())
                    {
                        throw std::invalid_argument("Unknown generated material resource: " + binding.name);
                    }
                    views.push_back(sampler ? found->second.sampler : found->second.texture);
                }
                else
                {
                    const auto found = std::ranges::find(resources, binding.name, &ShaderResourceViews::name);
                    if (found == resources.end() || found->type != binding.type || found->views.size() != binding.count)
                    {
                        throw std::invalid_argument(pass.definition.location.describe() +
                                                    ": missing or incompatible resource in Pass " +
                                                    pass.definition.name + ": " + binding.name);
                    }
                    views = found->views;
                }
                if (std::ranges::any_of(views,
                                        [](auto* view)
                                        {
                                            return view == nullptr;
                                        }))
                {
                    throw std::invalid_argument(pass.definition.location.describe() +
                                                ": unbound required shader resource: " + binding.name);
                }
                resolved.emplace(binding.name, std::move(views));
            }
            // Validate every binding before changing any live uniform or descriptor.
            bool encodingChanged = false;
            for (const auto& property : asset.properties)
            {
                if (property.normal)
                {
                    const bool bc5      = textures.at(property.name).bc5;
                    const auto previous = pass.normalEncodings.find(property.name);
                    encodingChanged =
                        encodingChanged || previous == pass.normalEncodings.end() || previous->second != bc5;
                }
            }
            if (pass.uniforms && (pass.revision != instance.revision() || encodingChanged))
            {
                auto        data     = instance.uniformData(asset, pass.program);
                const auto* uniforms = pass.program.parameters.field("material")->field("$element");
                for (const auto& property : asset.properties)
                {
                    if (property.normal)
                    {
                        const auto*    field    = uniforms->field(property.name + "Encoding");
                        const uint32_t encoding = textures.at(property.name).bc5 ? 1 : 0;
                        std::memcpy(data.data() + field->offset(ShaderOffsetKind::eUniform),
                                    &encoding,
                                    sizeof(encoding));
                    }
                }
                auto* mapped = device.core.MapBuffer(pass.uniforms->handle, 0, data.size());
                if (!mapped)
                {
                    throw std::runtime_error("Map shader material parameters");
                }
                std::memcpy(mapped, data.data(), data.size());
                device.core.UnmapBuffer(pass.uniforms->handle);
                pass.revision = instance.revision();
            }
            for (const auto& property : asset.properties)
            {
                if (property.normal)
                {
                    pass.normalEncodings.insert_or_assign(property.name, textures.at(property.name).bc5);
                }
            }
            for (auto& [name, views] : resolved)
            {
                // Borrowed descriptors can be destroyed and allocated at the same CPU address.
                // Always refresh their VRI contents at preparation, after the preceding GPU frame completes.
                const auto [space, range]          = pass.ranges.at(name);
                const auto                   index = std::ranges::find(pass.spaces, space) - pass.spaces.begin();
                VriDescriptorRangeUpdateDesc update {views.data(), uint32_t(views.size()), 0};
                device.core.UpdateDescriptorRanges(pass.sets[index], range, 1, &update);
            }
        }
    };

    ShaderMaterial::ShaderMaterial(Device&                                    device,
                                   const ShaderAsset&                         asset,
                                   MaterialInstance&                          instance,
                                   TextureResolver                            textures,
                                   std::span<const std::string_view>          requiredLightModes,
                                   const ShaderAsset::SubshaderCompatibility& compatible) :
        m_State(std::make_unique<State>(device, asset, instance, std::move(textures), requiredLightModes, compatible))
    {
    }

    ShaderMaterial::~ShaderMaterial() = default;

    VriPipeline* ShaderMaterial::prepare(std::string_view                     name,
                                         const ShaderPassContext&             context,
                                         std::span<const ShaderResourceViews> resources)
    {
        auto& state = *m_State;
        state.instance.validate(state.asset);
        const auto& definitions = state.asset.subshaders[state.selected].passes;
        const auto  found       = std::ranges::find(definitions, name, &ShaderPass::name);
        if (found == definitions.end())
        {
            throw std::invalid_argument("SubShader has no requested Pass: " + std::string(name));
        }
        if ((found->requiredFeatures & state.device.features) != found->requiredFeatures)
        {
            throw std::invalid_argument("Device lacks capabilities required by Pass: " + std::string(name));
        }
        auto&      pass     = state.pass(*found);
        const auto key      = detail::shaderPipelineKey(state.asset, state.instance, *found, context);
        auto       pipeline = state.pipelines.find(key);
        if (pipeline == state.pipelines.end())
        {
            VriPipeline* handle  = nullptr;
            const auto   shaders = pass.program.descriptors(found->entries);
            if (found->entries.front().stage == VriShaderStage_Compute)
            {
                if (!context.colors.empty() || context.depth != VriFormat_Unknown || !context.attributes.empty())
                {
                    throw std::invalid_argument("Compute Pass cannot use graphics attachments/input");
                }
                VriComputePipelineDesc desc {pass.layout, shaders.front(), state.device.pipelineCache};
                check(state.device.core.CreateComputePipeline(state.device.handle, &desc, &handle),
                      "Create shader compute pipeline");
            }
            else
            {
                uint32_t outputs = 0;
                for (const auto& shader : pass.program.shaders)
                {
                    outputs = std::max(outputs, shader.colorOutputs);
                }
                if (outputs != context.colors.size())
                {
                    throw std::invalid_argument("Pass " + found->name + " requires " + std::to_string(outputs) +
                                                " color attachments");
                }
                std::vector<VriColorAttachmentDesc> colors;
                for (const auto format : context.colors)
                {
                    colors.push_back({format, {}, VriColorWrite_RGBA});
                }
                VriGraphicsPipelineDesc desc {};
                desc.pipelineCache  = state.device.pipelineCache;
                desc.pipelineLayout = pass.layout;
                desc.shaders        = shaders.data();
                desc.shaderNum      = uint32_t(shaders.size());
                if (found->entries.front().stage == VriShaderStage_Vertex)
                {
                    desc.vertexInput = {context.attributes.data(),
                                        uint32_t(context.attributes.size()),
                                        context.streams.data(),
                                        uint32_t(context.streams.size())};
                }
                desc.inputAssembly.topology         = context.topology;
                desc.rasterization.cullMode         = VriCullMode_Back;
                desc.rasterization.frontFace        = VriFrontFace_CounterClockwise;
                desc.rasterization.lineWidth        = 1;
                desc.multisample.sampleNum          = context.samples;
                desc.depthStencil.depthTest         = context.depth != VriFormat_Unknown;
                desc.depthStencil.depthWrite        = desc.depthStencil.depthTest;
                desc.depthStencil.depthCompareOp    = VriCompareOp_LessOrEqual;
                desc.depthStencil.front.compareMask = 255;
                desc.depthStencil.front.writeMask   = 255;
                desc.depthStencil.front.compareOp   = VriCompareOp_Always;
                detail::applyShaderState(state.asset, state.instance, found->state, context, desc, colors);
                desc.outputMerger = {colors.data(), uint32_t(colors.size()), context.depth, context.viewMask};
                check(state.device.core.CreateGraphicsPipeline(state.device.handle, &desc, &handle),
                      "Create shader graphics pipeline");
            }
            State::Pipeline created {handle, &pass, {std::string(name), context, {resources.begin(), resources.end()}}};
            pipeline = state.pipelines.emplace(key, std::move(created)).first;
        }
        state.update(pass, resources);
        pipeline->second.prepared.resources.assign(resources.begin(), resources.end());
        state.active.insert_or_assign(std::string(name), &pipeline->second);
        return pipeline->second.handle;
    }

    void ShaderMaterial::bind(VriCommandBuffer* commands, std::string_view name) const
    {
        const auto found = m_State->active.find(name);
        if (found == m_State->active.end())
        {
            throw std::logic_error("Shader Pass was not prepared: " + std::string(name));
        }
        const auto& pipeline = *found->second;
        m_State->device.core.CmdSetPipelineLayout(commands, pipeline.pass->layout);
        m_State->device.core.CmdSetPipeline(commands, pipeline.handle);
        for (uint32_t index = 0; index < pipeline.pass->sets.size(); ++index)
        {
            m_State->device.core.CmdSetDescriptorSet(commands, index, pipeline.pass->sets[index]);
        }
    }

    void ShaderMaterial::bind(VriCommandBuffer* commands, VriPipeline* prepared) const
    {
        for (const auto& [key, pipeline] : m_State->pipelines)
        {
            if (pipeline.handle == prepared)
            {
                m_State->device.core.CmdSetPipelineLayout(commands, pipeline.pass->layout);
                m_State->device.core.CmdSetPipeline(commands, pipeline.handle);
                for (uint32_t index = 0; index < pipeline.pass->sets.size(); ++index)
                {
                    m_State->device.core.CmdSetDescriptorSet(commands, index, pipeline.pass->sets[index]);
                }
                return;
            }
        }
        throw std::invalid_argument("Pipeline was not prepared by this material");
    }

    const ShaderPass& ShaderMaterial::passForMode(std::string_view mode, bool mesh) const
    {
        for (const auto& pass : m_State->asset.subshaders[m_State->selected].passes)
        {
            const bool meshPass = std::ranges::any_of(pass.entries,
                                                      [](const ShaderEntry& entry)
                                                      {
                                                          return entry.stage == VriShaderStage_Mesh;
                                                      });
            if ((pass.lightMode == mode || (mode == "Forward" && pass.generated && pass.name == "MeshForward")) &&
                meshPass == mesh)
            {
                return pass;
            }
        }
        throw std::invalid_argument("Shader render contract has no compatible " + std::string(mode) + " Pass");
    }

    uint32_t ShaderMaterial::pushConstantSize(std::string_view name) const
    {
        return m_State->active.at(std::string(name))->pass->pushSize;
    }

    size_t ShaderMaterial::pipelineCount() const
    {
        return m_State->pipelines.size();
    }

    uint32_t ShaderMaterial::subshader() const
    {
        return m_State->selected;
    }

    std::vector<ShaderMaterial::PreparedPass> ShaderMaterial::preparedPasses() const
    {
        std::vector<PreparedPass> result;
        for (const auto& [name, pipeline] : m_State->active)
        {
            result.push_back(pipeline->prepared);
        }
        return result;
    }
} // namespace vultra
