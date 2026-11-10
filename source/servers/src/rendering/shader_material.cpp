#include "shader_pass_state.hpp"

#include <vultra/core/base/logger.hpp>
#include <vultra/drivers/rhi/shader_uniform_block.hpp>

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <stdexcept>

namespace vultra
{
    namespace
    {
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
            const ShaderPass&               definition;
            const ShaderProgram&            program;
            std::unique_ptr<ShaderBindings> parameters;
            bool                            hasUniforms = false;
            uint64_t                        revision    = UINT64_MAX;
            std::map<std::string, bool>     normalEncodings;

            Pass(const ShaderPass& definition, const ShaderProgram& program) :
                definition(definition),
                program(program)
            {
            }

        };

        struct Pipeline
        {
            Device&      device;
            VriPipeline* handle = nullptr;
            Pass*        pass;
            PreparedPass prepared;

            Pipeline(Device& device, Pass& pass) :
                device(device),
                pass(&pass)
            {
            }

            ~Pipeline()
            {
                if (handle)
                {
                    device.core.DestroyPipeline(handle);
                }
            }
        };

        Device&                                                device;
        const ShaderAsset&                                     asset;
        MaterialInstance&                                      instance;
        TextureResolverExpected                                              resolveTexture;
        uint32_t                                               selected       = 0;
        VriDescriptor*                                         defaultSampler = nullptr;
        std::map<std::string, std::unique_ptr<DefaultTexture>> defaults;
        std::map<std::pair<std::string, std::string>, std::unique_ptr<Pass>> passes;
        std::map<std::string, std::unique_ptr<Pipeline>>                     pipelines;
        std::map<std::string, Pipeline*, std::less<>>          active;

        State(Device& device, const ShaderAsset& asset, MaterialInstance& instance, TextureResolverExpected resolver) :
            device(device),
            asset(asset),
            instance(instance),
            resolveTexture(std::move(resolver))
        {
        }

        ~State()
        {
            if (!pipelines.empty() || !defaults.empty())
            {
                device.waitIdleForDestruction();
            }
            pipelines.clear();
            if (defaultSampler)
            {
                device.core.DestroyDescriptor(defaultSampler);
            }
        }

        std::expected<void, std::string> bind(VriCommandBuffer* commands, const Pipeline& pipeline) const
        {
            return pipeline.pass->parameters->bindExpected(commands, pipeline.handle);
        }

        std::expected<ShaderTextureBinding, std::string> texture(const ShaderProperty&     property,
                                                                 const ShaderTextureValue& value)
        {
            if (value.builtin.empty())
            {
                if (!resolveTexture)
                {
                    return std::unexpected("No texture resolver for property: " + property.name);
                }
                auto result = resolveTexture(property, value);
                if (!result)
                {
                    return std::unexpected(result.error());
                }
                if (!result->texture || !result->sampler || result->dimension != dimension(property.type))
                {
                    return std::unexpected("Texture resource/dimension mismatch: " + property.name);
                }
                if (result->srgb != property.srgb)
                {
                    return std::unexpected("Texture color-space/normal usage mismatch: " + property.name);
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
                if (const auto status = device.core.CreateSampler(device.handle, &sampler, &defaultSampler);
                    status != VriResult_Success)
                {
                    return std::unexpected("Create shader default sampler failed: " + std::to_string(status));
                }
            }
            const auto key   = value.builtin + std::to_string(uint32_t(property.type));
            auto       found = defaults.find(key);
            if (found == defaults.end())
            {
                auto desc           = colorTexture({1, 1}, VriFormat_RGBA16_SFLOAT);
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
                auto ownedTexture = Texture::create(device, desc);
                if (!ownedTexture)
                {
                    return std::unexpected(ownedTexture.error());
                }
                created->texture = std::move(*ownedTexture);
                auto upload      = Buffer::create(
                    device,
                    {uint64_t(desc.layerNum) * 512, 0, VriBufferUsage_TransferSrc, VriMemoryLocation_HostUpload});
                if (!upload)
                {
                    return std::unexpected(upload.error());
                }
                auto* mapped =
                    static_cast<std::byte*>(device.core.MapBuffer((*upload)->handle, 0, (*upload)->desc.size));
                if (!mapped)
                {
                    return std::unexpected("Map default shader texture");
                }
                // These three builtin colors are exactly representable in filterable RGBA16F.
                std::array<uint16_t, 4> color {0x3c00, 0x3c00, 0x3c00, 0x3c00};
                if (value.builtin == "black")
                {
                    color = {0, 0, 0, 0x3c00};
                }
                else if (value.builtin == "normal")
                {
                    color = {0x3800, 0x3800, 0x3c00, 0x3c00};
                }
                for (uint32_t layer = 0; layer < desc.layerNum; ++layer)
                {
                    std::memcpy(mapped + uint64_t(layer) * 512, color.data(), sizeof(color));
                }
                device.core.UnmapBuffer((*upload)->handle);
                auto frame = Frame::create(device);
                if (!frame)
                {
                    return std::unexpected(frame.error());
                }
                auto commands = (*frame)->beginExpected();
                if (!commands)
                {
                    return std::unexpected(commands.error());
                }
                VriTextureViewDesc whole {};
                whole.viewType = VriTextureViewType_MaxEnum;
                if (auto transitioned = created->texture->transitionExpected(
                        *commands,
                        {VriAccess_CopyDestinationWrite, VriLayout_CopyDestination, VriPipelineStage_Transfer},
                        whole);
                    !transitioned)
                {
                    return std::unexpected(transitioned.error());
                }
                for (uint32_t layer = 0; layer < desc.layerNum; ++layer)
                {
                    VriBufferTextureCopyDesc copy {};
                    copy.bufferOffset      = uint64_t(layer) * 512;
                    copy.bufferRowLength   = 32;
                    copy.bufferImageHeight = 1;
                    copy.texture           = {0, layer, 1, VriImageAspect_Color, 0, 0, 0, 1, 1, 1};
                    device.core.CmdUploadBufferToTexture(*commands, created->texture->handle, (*upload)->handle, &copy);
                }
                if (auto transitioned = created->texture->transitionExpected(
                        *commands,
                        {VriAccess_ShaderResourceRead, VriLayout_ShaderResource, VriPipelineStage_AllCommands},
                        whole);
                    !transitioned)
                {
                    return std::unexpected(transitioned.error());
                }
                if (auto submitted = (*frame)->submitAndWaitExpected(); !submitted)
                {
                    return std::unexpected(submitted.error());
                }
                VriTextureViewDesc view {};
                view.texture  = created->texture->handle;
                view.viewType = viewType;
                view.format   = desc.format;
                view.aspect   = VriImageAspect_Color;
                view.layerNum = desc.layerNum;
                if (const auto status = device.core.CreateTextureView(device.handle, &view, &created->view);
                    status != VriResult_Success)
                {
                    return std::unexpected("Create default shader texture view failed: " + std::to_string(status));
                }
                found = defaults.emplace(key, std::move(created)).first;
            }
            return ShaderTextureBinding {found->second->view, defaultSampler, dimension(property.type), false, false};
        }

        std::expected<std::unique_ptr<Pass>, std::string> createPass(const ShaderPass& definition)
        {
            auto program = definition.programExpected(instance.variant);
            if (!program)
            {
                return std::unexpected(program.error());
            }
            auto reflected = (**program).resourceBindingsExpected();
            if (!reflected)
            {
                return std::unexpected(reflected.error());
            }
            const bool       hasUniforms = std::ranges::any_of(*reflected,
                                                         [](const auto& binding)
                                                         {
                                                             return !binding.pushConstant && binding.name == "material";
                                                         });
            const std::array names {std::string_view("material")};
            auto             parameters = ShaderBindings::create(device,
                                                     **program,
                                                     definition.entries,
                                                     hasUniforms ? std::span<const std::string_view>(names) :
                                                                               std::span<const std::string_view> {});
            if (!parameters)
            {
                return std::unexpected(parameters.error());
            }
            auto candidate         = std::make_unique<Pass>(definition, **program);
            candidate->parameters = std::move(*parameters);
            candidate->hasUniforms = hasUniforms;
            return candidate;
        }

        std::expected<void, std::string> update(Pass& pass, std::span<const ShaderResourceViews> resources)
        {
            const auto                                  revision = instance.revision();
            const auto                                  variant  = instance.variant;
            std::map<std::string, ShaderTextureBinding> textures;
            for (const auto& property : asset.properties)
            {
                if (property.isTexture())
                {
                    auto value = instance.valueExpected(asset, property.name);
                    if (!value)
                    {
                        return std::unexpected(value.error());
                    }
                    auto resolved = texture(property, std::get<ShaderTextureValue>(*value));
                    if (!resolved)
                    {
                        return std::unexpected(resolved.error());
                    }
                    textures.emplace(property.name, *resolved);
                }
            }
            if (instance.revision() != revision || instance.variant != variant)
            {
                return std::unexpected("Material changed while resolving textures");
            }
            for (size_t i = 0; i < resources.size(); ++i)
            {
                if (std::ranges::any_of(resources.subspan(i + 1),
                                        [&](const auto& other)
                                        {
                                            return other.name == resources[i].name;
                                        }))
                {
                    return std::unexpected("Duplicate shader resource binding: " + resources[i].name);
                }
            }
            std::vector<ShaderResourceViews> resolved;
            for (const auto& binding : pass.parameters->bindings())
            {
                if (binding.pushConstant)
                {
                    continue;
                }
                std::vector<const VriDescriptor*> views;
                if (binding.name == "material")
                {
                    continue;
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
                        return std::unexpected("Unknown generated material resource: " + binding.name);
                    }
                    views.push_back(sampler ? found->second.sampler : found->second.texture);
                }
                else
                {
                    const auto found = std::ranges::find(resources, binding.name, &ShaderResourceViews::name);
                    if (found == resources.end() || found->type != binding.type || found->views.size() != binding.count)
                    {
                        return std::unexpected(pass.definition.location.describe() +
                                               ": missing or incompatible resource in Pass " + pass.definition.name +
                                               ": " + binding.name);
                    }
                    views = found->views;
                }
                if (std::ranges::any_of(views,
                                        [](auto* view)
                                        {
                                            return view == nullptr;
                                        }))
                {
                    return std::unexpected(pass.definition.location.describe() +
                                           ": unbound required shader resource: " + binding.name);
                }
                resolved.push_back({binding.name, binding.type, std::move(views)});
            }
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
            std::optional<ShaderUniformBlock> data;
            if (pass.hasUniforms && (pass.revision != instance.revision() || encodingChanged))
            {
                auto packed = instance.uniformBlockExpected(asset, pass.program);
                if (!packed)
                {
                    return std::unexpected(packed.error());
                }
                data                 = std::move(*packed);
                for (const auto& property : asset.properties)
                {
                    if (property.normal)
                    {
                        const uint32_t encoding = textures.at(property.name).bc5 ? 1 : 0;
                        const auto       path     = property.name + "Encoding";
                        const std::array writes {ShaderUniformWrite {path, std::span(&encoding, 1)}};
                        if (auto written = data->setExpected(writes); !written)
                        {
                            return std::unexpected(written.error());
                        }
                    }
                }
            }
            std::optional<ShaderUniformUpload>   upload;
            std::span<const ShaderUniformUpload> uploads;
            if (data)
            {
                upload.emplace("material", *data);
                uploads = {&*upload, 1};
            }
            if (auto updated = pass.parameters->updateExpected(resolved, uploads); !updated)
            {
                return updated;
            }
            pass.revision = instance.revision();
            for (const auto& property : asset.properties)
            {
                if (property.normal)
                {
                    pass.normalEncodings.insert_or_assign(property.name, textures.at(property.name).bc5);
                }
            }
            return {};
        }
    };

    ShaderMaterial::ShaderMaterial(std::unique_ptr<State> state) :
        m_State(std::move(state))
    {
    }

    std::expected<std::unique_ptr<ShaderMaterial>, std::string>
    ShaderMaterial::create(Device&                                            device,
                           const ShaderAsset&                                 asset,
                           MaterialInstance&                                  instance,
                           TextureResolverExpected                            textures,
                           std::span<const std::string_view>                  requiredLightModes,
                           const ShaderAsset::SubshaderCompatibilityExpected& compatible)
    {
        if (auto valid = asset.validateExpected(); !valid)
        {
            return std::unexpected(valid.error());
        }
        auto        state = std::make_unique<State>(device, asset, instance, std::move(textures));
        std::string diagnostics;
        auto        selected =
            asset.selectSubshaderExpected("Vultra", device.features, requiredLightModes, diagnostics, compatible);
        if (!selected)
        {
            return std::unexpected(selected.error());
        }
        if (auto valid = instance.validateExpected(asset); !valid)
        {
            return std::unexpected(valid.error());
        }
        state->selected = *selected;
        Logger::core().info("Shader {} selected SubShader {}\n{}", asset.name, state->selected, diagnostics);
        return std::unique_ptr<ShaderMaterial>(new ShaderMaterial(std::move(state)));
    }

    ShaderMaterial::ShaderMaterial(Device&                                    device,
                                   const ShaderAsset&                         asset,
                                   MaterialInstance&                          instance,
                                   TextureResolver                            textures,
                                   std::span<const std::string_view>          requiredLightModes,
                                   const ShaderAsset::SubshaderCompatibility& compatible)
    {
        // Explicit adapters for existing throwing application callbacks during migration.
        TextureResolverExpected checkedTextures;
        if (textures)
        {
            checkedTextures =
                [textures = std::move(textures)](const auto& property,
                                                 const auto& value) -> std::expected<ShaderTextureBinding, std::string>
            {
                try
                {
                    return textures(property, value);
                }
                catch (const std::exception& error)
                {
                    return std::unexpected(error.what());
                }
            };
        }
        ShaderAsset::SubshaderCompatibilityExpected checkedCompatibility;
        if (compatible)
        {
            checkedCompatibility = [compatible](const auto& subshader) -> std::expected<std::string, std::string>
            {
                try
                {
                    return compatible(subshader);
                }
                catch (const std::exception& error)
                {
                    return std::unexpected(error.what());
                }
            };
        }
        auto material =
            create(device, asset, instance, std::move(checkedTextures), requiredLightModes, checkedCompatibility);
        if (!material)
        {
            throw std::invalid_argument(material.error());
        }
        m_State = std::move((*material)->m_State);
    }

    ShaderMaterial::~ShaderMaterial() = default;

    VriPipeline* ShaderMaterial::prepare(std::string_view                     name,
                                         const ShaderPassContext&             context,
                                         std::span<const ShaderResourceViews> resources)
    {
        auto prepared = prepareExpected(name, context, resources);
        if (!prepared)
        {
            throw std::invalid_argument(prepared.error());
        }
        return *prepared;
    }

    std::expected<VriPipeline*, std::string>
    ShaderMaterial::prepareExpected(std::string_view                     name,
                                    const ShaderPassContext&             context,
                                    std::span<const ShaderResourceViews> resources)
    {
        auto& state = *m_State;
        if (auto valid = state.instance.validateExpected(state.asset); !valid)
        {
            return std::unexpected(valid.error());
        }
        const auto& definitions = state.asset.subshaders[state.selected].passes;
        const auto  found       = std::ranges::find(definitions, name, &ShaderPass::name);
        if (found == definitions.end())
        {
            return std::unexpected("SubShader has no requested Pass: " + std::string(name));
        }
        if ((found->requiredFeatures & state.device.features) != found->requiredFeatures)
        {
            return std::unexpected("Device lacks capabilities required by Pass: " + std::string(name));
        }
        auto key = detail::shaderPipelineKeyExpected(state.asset, state.instance, *found, context);
        if (!key)
        {
            return std::unexpected(key.error());
        }
        const auto                   passKey      = std::pair(found->name, state.instance.variant);
        const auto                   existingPass = state.passes.find(passKey);
        std::unique_ptr<State::Pass> candidatePass;
        State::Pass*                 pass = nullptr;
        if (existingPass == state.passes.end())
        {
            auto created = state.createPass(*found);
            if (!created)
            {
                return std::unexpected(created.error());
            }
            candidatePass = std::move(*created);
            pass          = candidatePass.get();
        }
        else
        {
            pass = existingPass->second.get();
        }
        auto cached = state.pipelines.find(*key);
        // Candidate pipeline ownership ends before its borrowed candidate layout on any rejection.
        std::unique_ptr<State::Pipeline> candidatePipeline;
        if (cached == state.pipelines.end())
        {
            candidatePipeline = std::make_unique<State::Pipeline>(state.device, *pass);
            auto shaders      = pass->program.descriptorsExpected(found->entries);
            if (!shaders)
            {
                return std::unexpected(shaders.error());
            }
            if (found->entries.front().stage == VriShaderStage_Compute)
            {
                if (!context.colors.empty() || context.depth != VriFormat_Unknown || !context.attributes.empty())
                {
                    return std::unexpected("Compute Pass cannot use graphics attachments/input");
                }
                const VriComputePipelineDesc desc {pass->parameters->layout(),
                                                   shaders->front(),
                                                   state.device.pipelineCache};
                const auto                   status =
                    state.device.core.CreateComputePipeline(state.device.handle, &desc, &candidatePipeline->handle);
                if (status != VriResult_Success)
                {
                    return std::unexpected("Create shader compute pipeline failed: " + std::to_string(status));
                }
            }
            else
            {
                uint32_t outputs = 0;
                for (const auto& shader : pass->program.shaders)
                {
                    outputs = std::max(outputs, shader.colorOutputs);
                }
                if (outputs != context.colors.size())
                {
                    return std::unexpected("Pass " + found->name + " requires " + std::to_string(outputs) +
                                           " color attachments");
                }
                std::vector<VriColorAttachmentDesc> colors;
                for (const auto format : context.colors)
                {
                    colors.push_back({format, {}, VriColorWrite_RGBA});
                }
                VriGraphicsPipelineDesc desc {};
                desc.pipelineCache  = state.device.pipelineCache;
                desc.pipelineLayout = pass->parameters->layout();
                desc.shaders        = shaders->data();
                desc.shaderNum      = uint32_t(shaders->size());
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
                if (auto applied = detail::applyShaderStateExpected(state.asset,
                                                                    state.instance,
                                                                    found->state,
                                                                    context,
                                                                    desc,
                                                                    colors);
                    !applied)
                {
                    return std::unexpected(applied.error());
                }
                desc.outputMerger = {colors.data(), uint32_t(colors.size()), context.depth, context.viewMask};
                const auto status =
                    state.device.core.CreateGraphicsPipeline(state.device.handle, &desc, &candidatePipeline->handle);
                if (status != VriResult_Success)
                {
                    return std::unexpected("Create shader graphics pipeline failed: " + std::to_string(status));
                }
            }
            if (!candidatePipeline->handle)
            {
                return std::unexpected("Shader pipeline creation returned no pipeline");
            }
            candidatePipeline->prepared = {std::string(name), context, {}};
        }
        // Every fallible resource/uniform check precedes mutation of accepted descriptors and active selection.
        if (auto updated = state.update(*pass, resources); !updated)
        {
            return std::unexpected(updated.error());
        }
        if (candidatePass)
        {
            state.passes.emplace(passKey, std::move(candidatePass));
        }
        if (candidatePipeline)
        {
            cached = state.pipelines.emplace(std::move(*key), std::move(candidatePipeline)).first;
        }
        auto& pipeline = *cached->second;
        pipeline.prepared.resources.assign(resources.begin(), resources.end());
        state.active.insert_or_assign(std::string(name), &pipeline);
        return pipeline.handle;
    }

    void ShaderMaterial::bind(VriCommandBuffer* commands, std::string_view name) const
    {
        if (auto bound = bindExpected(commands, name); !bound)
        {
            throw std::invalid_argument(bound.error());
        }
    }

    std::expected<void, std::string> ShaderMaterial::bindExpected(VriCommandBuffer* commands,
                                                                  std::string_view  name) const
    {
        if (!commands)
        {
            return std::unexpected("Shader material binding requires a command buffer");
        }
        const auto found = m_State->active.find(name);
        if (found == m_State->active.end())
        {
            return std::unexpected("Shader Pass was not prepared: " + std::string(name));
        }
        return m_State->bind(commands, *found->second);
    }

    void ShaderMaterial::bind(VriCommandBuffer* commands, VriPipeline* prepared) const
    {
        if (auto bound = bindExpected(commands, prepared); !bound)
        {
            throw std::invalid_argument(bound.error());
        }
    }

    std::expected<void, std::string> ShaderMaterial::bindExpected(VriCommandBuffer* commands,
                                                                  VriPipeline*      prepared) const
    {
        if (!commands)
        {
            return std::unexpected("Shader material binding requires a command buffer");
        }
        for (const auto& [key, owned] : m_State->pipelines)
        {
            const auto& pipeline = *owned;
            if (pipeline.handle == prepared)
            {
                return m_State->bind(commands, pipeline);
            }
        }
        return std::unexpected("Pipeline was not prepared by this material");
    }

    const ShaderPass& ShaderMaterial::passForMode(std::string_view mode, bool mesh) const
    {
        auto pass = passForModeExpected(mode, mesh);
        if (!pass)
        {
            throw std::invalid_argument(pass.error());
        }
        return **pass;
    }

    std::expected<const ShaderPass*, std::string> ShaderMaterial::passForModeExpected(std::string_view mode,
                                                                                      bool             mesh) const
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
                return &pass;
            }
        }
        return std::unexpected("Shader render contract has no compatible " + std::string(mode) + " Pass");
    }

    uint32_t ShaderMaterial::pushConstantSize(std::string_view name) const
    {
        auto size = pushConstantSizeExpected(name);
        if (!size)
        {
            throw std::invalid_argument(size.error());
        }
        return *size;
    }

    std::expected<uint32_t, std::string> ShaderMaterial::pushConstantSizeExpected(std::string_view name) const
    {
        const auto found = m_State->active.find(name);
        if (found == m_State->active.end())
        {
            return std::unexpected("Shader Pass was not prepared: " + std::string(name));
        }
        return found->second->pass->parameters->pushConstantSize();
    }

    bool ShaderMaterial::belongsTo(const Device& device) const
    {
        return &device == &m_State->device;
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
