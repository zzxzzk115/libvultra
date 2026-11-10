#include <vultra/assets/material_block.hpp>
#include <vultra/drivers/rhi/image_readback.hpp>
#include <vultra/drivers/rhi/shader_pipeline.hpp>
#include <vultra/servers/rendering/texture_upload.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace
{
    using namespace vultra;
    using Result = std::expected<void, std::string>;
    const std::array entries {ShaderEntry {"main", VriShaderStage_Compute}};
    constexpr Extent kSize {31, 17};

    AssetId id(uint8_t value)
    {
        AssetId result;
        result.value.bytes.back() = value;
        return result;
    }

    Result initialize(MaterialBlock& block)
    {
        const std::array basis {1.f, 2.f, 3.f, 4.f};
        const std::array tint0 {2.f, 3.f, 4.f};
        const std::array tint1 {4.f, 5.f, 6.f};
        const std::array gain0 {2.f};
        const std::array gain1 {3.f};
        const std::array exposure {0.5f};
        const std::array enabled {1u};
        const std::array bias0 {0.5f};
        const std::array bias1 {0.25f};
        const std::array fields {
            ShaderUniformWrite {"basis", std::span(basis), ShaderParameterKind::eMatrix, 2, 2},
            ShaderUniformWrite {"layers[0].tint", std::span(tint0), ShaderParameterKind::eVector, 1, 3},
            ShaderUniformWrite {"layers[1].tint", std::span(tint1), ShaderParameterKind::eVector, 1, 3},
            ShaderUniformWrite {"layers[0].gain", std::span(gain0)},
            ShaderUniformWrite {"layers[1].gain", std::span(gain1)},
            ShaderUniformWrite {"exposure", std::span(exposure)},
            ShaderUniformWrite {"enabled", std::span(enabled)}};
        const std::array controls0 {ShaderUniformWrite {"bias", std::span(bias0)}};
        const std::array controls1 {ShaderUniformWrite {"bias", std::span(bias1)}};
        const std::array uniforms {MaterialUniformWrite {"material", fields},
                                   MaterialUniformWrite {"material.controls[0]", controls0},
                                   MaterialUniformWrite {"material.controls[1]", controls1}};
        const std::array colors {id(1), id(2)};
        const std::array nearest {id(3)};
        const std::array resources {MaterialResourceWrite {"material.colors", colors},
                                    MaterialResourceWrite {"material.nearest", nearest}};
        auto             changed = block.setExpected(uniforms, resources);
        if (!changed || !*changed || block.revision() != 1)
        {
            return std::unexpected("Initialize generic nested material block failed");
        }
        auto identical = block.setExpected(uniforms, resources);
        if (!identical || *identical || block.revision() != 1)
        {
            return std::unexpected("Byte-identical material edit changed its revision");
        }
        return {};
    }

    Result persistence(MaterialBlock& block, const std::filesystem::path& scratch)
    {
        auto text = block.serializeExpected();
        if (!text)
        {
            return std::unexpected(text.error());
        }
        auto parsed = MaterialBlock::parseExpected(*text);
        if (!parsed)
        {
            return std::unexpected(parsed.error());
        }
        auto again = parsed->serializeExpected();
        if (!again || *again != *text || parsed->revision() != 0)
        {
            return std::unexpected("Material block did not round trip layout, packed bytes and AssetIds exactly");
        }
        std::error_code error;
        std::filesystem::create_directories(scratch, error);
        if (error)
        {
            return std::unexpected(error.message());
        }
        std::ofstream file(scratch / "material.json", std::ios::binary);
        file << *text;
        file.close();
        if (!file)
        {
            return std::unexpected("Write generic material snapshot failed");
        }
        const auto original = nlohmann::json::parse(*text);
        for (int edit = 0; edit < 24; ++edit)
        {
            auto  document = original;
            auto& uniform  = document["uniforms"][0];
            auto& resource = document["resources"][0];
            switch (edit)
            {
                case 0:
                    document["version"] = 2;
                    break;
                case 1:
                    document["kind"] = "other";
                    break;
                case 2:
                    document["byte_order"] = "other";
                    break;
                case 3:
                    document["scope"] = "different";
                    break;
                case 4:
                    document.erase("uniforms");
                    break;
                case 5:
                    document["resources"] = false;
                    break;
                case 6:
                    document["uniforms"].push_back(uniform);
                    break;
                case 7:
                    document["resources"].push_back(resource);
                    break;
                case 8:
                    uniform["binding"] = "outside";
                    break;
                case 9:
                    uniform["bytes"] = "z0";
                    break;
                case 10:
                    uniform["bytes"] = "0";
                    break;
                case 11:
                    uniform["bytes"] = "00";
                    break;
                case 12:
                    uniform["layout"]["size"] = 16777217u;
                    break;
                case 13:
                    uniform["layout"]["kind"] = 100u;
                    break;
                case 14:
                    uniform["layout"]["fields"][0]["scalar"] = 100u;
                    break;
                case 15:
                    uniform.erase("layout");
                    break;
                case 16:
                    resource["binding"] = "outside";
                    break;
                case 17:
                    resource["type"] = 999u;
                    break;
                case 18:
                    resource["dimension"] = 999u;
                    break;
                case 19:
                    resource["assets"] = nlohmann::json::array();
                    break;
                case 20:
                    resource["assets"][0] = "not-an-id";
                    break;
                case 21:
                    resource["assets"][0] = false;
                    break;
                case 22:
                    document["scope"] = std::string("a\0b", 3);
                    break;
                case 23:
                    resource["binding"] = uniform["binding"];
                    break;
            }
            if (MaterialBlock::parseExpected(document.dump()))
            {
                return std::unexpected("Malformed material snapshot accepted, edit " + std::to_string(edit));
            }
        }
        if (MaterialBlock::parseExpected("{") || MaterialBlock::parseExpected(std::string("\xff", 1)))
        {
            return std::unexpected("Invalid JSON/UTF-8 material snapshot accepted");
        }
        block = std::move(*parsed);
        std::puts(
            "Material block persistence: exact layout/byte/reference round trip and 26 malformed inputs rejected");
        return {};
    }

    struct Faults
    {
        Device&               device;
        VriCoreInterface      original;
        unsigned              rejectMap      = 0;
        unsigned              maps           = 0;
        bool                  rejectPipeline = false;
        inline static Faults* s_Active       = nullptr;

        explicit Faults(Device& value) :
            device(value),
            original(value.core)
        {
            s_Active                          = this;
            device.core.MapBuffer             = map;
            device.core.CreateComputePipeline = pipeline;
        }

        ~Faults()
        {
            device.core = original;
            s_Active    = nullptr;
        }

        static void* VRI_CALL map(VriBuffer* buffer, uint64_t offset, uint64_t size)
        {
            auto& self = *s_Active;
            return ++self.maps == self.rejectMap ? nullptr : self.original.MapBuffer(buffer, offset, size);
        }

        static VriResult VRI_CALL pipeline(VriDevice* device, const VriComputePipelineDesc* desc, VriPipeline** output)
        {
            return s_Active->rejectPipeline ? VriResult_Failure :
                                              s_Active->original.CreateComputePipeline(device, desc, output);
        }
    };

    struct Gpu
    {
        std::unique_ptr<ShaderBindings> bindings;
        std::unique_ptr<ShaderPipeline> pipeline;
    };

    Result update(Gpu& gpu, const MaterialBlock& block, const MaterialBlock::Resolver& resolver, VriDescriptor* output)
    {
        auto resources = block.resolveResourcesExpected(resolver);
        if (!resources)
        {
            return std::unexpected(resources.error());
        }
        resources->push_back({"output", VriDescriptorType_StorageTexture, {output}});
        std::vector<ShaderUniformUpload> uploads;
        for (const auto& [name, uniform] : block.uniforms())
        {
            uploads.emplace_back(name, uniform);
        }
        return gpu.bindings->updateExpected(*resources, uploads);
    }

    std::expected<Gpu, std::string> prepare(Device&                        device,
                                            const ShaderProgram&           program,
                                            const MaterialBlock&           block,
                                            const MaterialBlock::Resolver& resolver,
                                            VriDescriptor*                 output)
    {
        std::vector<std::string_view> names;
        for (const auto& [name, uniform] : block.uniforms())
        {
            names.push_back(name);
        }
        auto bindings = ShaderBindings::create(device, program, entries, names);
        if (!bindings)
        {
            return std::unexpected(bindings.error());
        }
        Gpu gpu {std::move(*bindings), {}};
        if (auto uploaded = update(gpu, block, resolver, output); !uploaded)
        {
            return std::unexpected(uploaded.error());
        }
        auto pipeline = ShaderPipeline::create(
            device,
            program,
            {entries.begin(), entries.end()},
            [&](std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, std::string>
            {
                VriPipeline*                 result = nullptr;
                const VriComputePipelineDesc desc {gpu.bindings->layout(), shaders.front(), device.pipelineCache};
                const auto                   status = device.core.CreateComputePipeline(device.handle, &desc, &result);
                if (status != VriResult_Success || !result)
                {
                    if (result)
                    {
                        device.core.DestroyPipeline(result);
                    }
                    return std::unexpected("Create generic material fixture pipeline failed");
                }
                return result;
            });
        if (!pipeline)
        {
            return std::unexpected(pipeline.error());
        }
        gpu.pipeline = std::move(*pipeline);
        return gpu;
    }

    Result run(const ShaderProgram& oldProgram, const ShaderProgram& newProgram, const std::filesystem::path& scratch)
    {
        auto block = MaterialBlock::create(oldProgram, "material");
        if (!block)
        {
            return std::unexpected(block.error());
        }
        if (block->uniforms().size() != 3 || block->resources().size() != 2 ||
            MaterialBlock::create(oldProgram, "output"))
        {
            return std::unexpected("Generic material scope did not isolate its nested CB and resources");
        }
        if (auto initialized = initialize(*block); !initialized)
        {
            return initialized;
        }
        if (auto persisted = persistence(*block, scratch); !persisted)
        {
            return persisted;
        }
        auto arrayScope   = MaterialBlock::create(oldProgram, "material.controls");
        auto scopeText    = arrayScope ? arrayScope->serializeExpected() : std::unexpected(arrayScope.error());
        auto scopeRestore = scopeText ? MaterialBlock::parseExpected(*scopeText) : std::unexpected(scopeText.error());
        if (!scopeRestore || scopeRestore->uniforms().size() != 2 || !scopeRestore->resources().empty() ||
            !scopeRestore->uniforms().contains("material.controls[1]") ||
            scopeRestore->serializeExpected() != scopeText)
        {
            return std::unexpected("Constant-buffer array scope did not preserve its independent indexed values");
        }
        auto device = Device::create();
        if (!device)
        {
            return std::unexpected(device.error());
        }
        auto&                                   gpuDevice = **device;
        std::array<std::unique_ptr<Texture>, 2> colors;
        std::array<VriDescriptor*, 2>           colorViews {};
        for (size_t index = 0; index < colors.size(); ++index)
        {
            TextureData data {
                TextureFormat::eRgba8Unorm,
                {{{1, 1},
                  {std::byte(index == 0 ? 255 : 0), std::byte(0), std::byte(index == 1 ? 255 : 0), std::byte(255)}}}};
            auto texture = uploadTextureAssetExpected(gpuDevice, data);
            if (!texture)
            {
                return std::unexpected(texture.error());
            }
            auto view = (*texture)->viewExpected();
            if (!view)
            {
                return std::unexpected(view.error());
            }
            colorViews[index] = *view;
            colors[index]     = std::move(*texture);
        }
        auto desc   = colorTexture(kSize, VriFormat_RGBA32_SFLOAT);
        desc.usage  = VriTextureUsage_ShaderResourceStorage | VriTextureUsage_TransferSrc;
        auto output = Texture::create(gpuDevice, desc);
        if (!output)
        {
            return std::unexpected(output.error());
        }
        auto outputView = (*output)->viewExpected();
        if (!outputView)
        {
            return std::unexpected(outputView.error());
        }
        VriSamplerDesc samplerDesc {};
        samplerDesc.minFilter        = VriFilter_Nearest;
        samplerDesc.magFilter        = VriFilter_Nearest;
        samplerDesc.mipmapMode       = VriMipmapMode_Nearest;
        samplerDesc.addressModeU     = VriAddressMode_ClampToEdge;
        samplerDesc.addressModeV     = VriAddressMode_ClampToEdge;
        samplerDesc.addressModeW     = VriAddressMode_ClampToEdge;
        VriDescriptor* samplerHandle = nullptr;
        if (gpuDevice.core.CreateSampler(gpuDevice.handle, &samplerDesc, &samplerHandle) != VriResult_Success)
        {
            return std::unexpected("Create generic material sampler failed");
        }
        auto destroySampler = [&](VriDescriptor* value)
        {
            gpuDevice.core.DestroyDescriptor(value);
        };
        std::unique_ptr<VriDescriptor, decltype(destroySampler)> sampler(samplerHandle, destroySampler);
        bool                                                     rejectResource = false;
        bool                                                     wrongDimension = false;
        const MaterialBlock::Resolver                            resolver       = [&](std::string_view,
                                                     AssetId asset) -> std::expected<MaterialResourceView, std::string>
        {
            if (rejectResource)
            {
                return std::unexpected("Injected material resource resolution failure");
            }
            if (asset == id(3))
            {
                return MaterialResourceView {sampler.get(), VriDescriptorType_Sampler};
            }
            if (asset != id(1) && asset != id(2))
            {
                return std::unexpected("Unknown material resource AssetId");
            }
            return MaterialResourceView {colorViews[asset == id(1) ? 0 : 1],
                                         VriDescriptorType_Texture,
                                         wrongDimension ? VriTextureViewType_3D : VriTextureViewType_2D};
        };
        auto gpu = prepare(gpuDevice, oldProgram, *block, resolver, *outputView);
        if (!gpu)
        {
            return std::unexpected(gpu.error());
        }
        auto frame = Frame::create(gpuDevice);
        if (!frame)
        {
            return std::unexpected(frame.error());
        }
        const auto render = [&](std::array<float, 4> expected, ShaderBindings* selected = nullptr) -> Result
        {
            auto& bindings = selected ? *selected : *gpu->bindings;
            auto commands = (*frame)->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            VriTextureViewDesc full {};
            full.viewType = VriTextureViewType_2D;
            full.aspect   = VriImageAspect_Color;
            for (auto& texture : colors)
            {
                if (auto transitioned = texture->transitionExpected(
                        *commands,
                        {VriAccess_ShaderResourceRead, VriLayout_ShaderResource, VriPipelineStage_ComputeShader},
                        full);
                    !transitioned)
                {
                    return transitioned;
                }
            }
            if (auto transitioned = (*output)->transitionExpected(*commands,
                                                                  {VriAccess_ShaderResourceStorageWrite,
                                                                   VriLayout_ShaderResourceStorage,
                                                                   VriPipelineStage_ComputeShader},
                                                                  full);
                !transitioned)
            {
                return transitioned;
            }
            for (const auto& [name, uniform] : block->uniforms())
            {
                auto buffer = bindings.uniformBufferExpected(name);
                if (!buffer)
                {
                    return std::unexpected(buffer.error());
                }
                if (auto transitioned =
                        (*buffer)->transitionExpected(*commands,
                                                      {VriAccess_ConstantBufferRead, VriPipelineStage_ComputeShader});
                    !transitioned)
                {
                    return transitioned;
                }
            }
            if (auto bound = bindings.bindExpected(*commands, gpu->pipeline->handle()); !bound)
            {
                return bound;
            }
            const VriDispatchDesc dispatch {4, 3, 1};
            gpuDevice.core.CmdDispatch(*commands, &dispatch);
            if (auto submitted = (*frame)->submitAndWaitExpected(); !submitted)
            {
                return submitted;
            }
            auto image = readbackExpected(gpuDevice, **output);
            if (!image)
            {
                return std::unexpected(image.error());
            }
            for (size_t pixel = 0; pixel < image->rgba.size(); pixel += 4)
            {
                for (size_t channel = 0; channel < 4; ++channel)
                {
                    if (image->rgba[pixel + channel] != expected[channel])
                    {
                        return std::unexpected("Generic material pixel oracle differs: " +
                                               std::to_string(image->rgba[pixel + channel]));
                    }
                }
            }
            return {};
        };
        if (auto checked = render({8.5f, 5.5f, 18.5f, 0.25f}); !checked)
        {
            return checked;
        }
        {
            const std::array<std::vector<std::string_view>, 5> invalid {
                std::vector<std::string_view> {"material.controls[0]"},
                std::vector<std::string_view> {"material.controls[2]"},
                std::vector<std::string_view> {"material.controls[01]"},
                std::vector<std::string_view> {"material.controls[0]", "material.controls[0]"},
                std::vector<std::string_view> {"material.controls", "material.controls[0]"}};
            for (const auto& names : invalid)
            {
                if (ShaderBindings::create(gpuDevice, oldProgram, entries, names))
                {
                    return std::unexpected("Owned uniform array accepted partial, invalid or duplicate elements");
                }
            }
            auto oversized = oldProgram;
            auto material  = std::ranges::find(oversized.parameters.fields, "material", &ShaderParameter::name);
            if (material == oversized.parameters.fields.end())
            {
                return std::unexpected("Uniform ownership limit fixture lacks its material scope");
            }
            auto element = std::ranges::find(material->fields, "$element", &ShaderParameter::name);
            if (element == material->fields.end())
            {
                return std::unexpected("Uniform ownership limit fixture lacks its element");
            }
            auto controls = std::ranges::find(element->fields, "controls", &ShaderParameter::name);
            if (controls == element->fields.end())
            {
                return std::unexpected("Uniform ownership limit fixture lacks its array");
            }
            controls->count = 65536;
            const std::array<std::string_view, 2> names {"material", "material.controls"};
            auto excessive = ShaderBindings::create(gpuDevice, oversized, entries, names);
            if (excessive || excessive.error().find("Owned uniform count") == std::string::npos ||
                MaterialBlock::create(oversized, "material"))
            {
                return std::unexpected("Aggregate uniform ownership limit was not enforced");
            }
            auto aggregate = ShaderBindings::create(gpuDevice, oldProgram, entries, names);
            auto instance  = gpu->bindings->createInstanceExpected();
            if (!aggregate || !instance || (*instance)->layout() != gpu->bindings->layout())
            {
                return std::unexpected("Create aggregate uniform array or independent instance failed");
            }
            for (const auto& [name, uniform] : block->uniforms())
            {
                auto original    = gpu->bindings->uniformBufferExpected(name);
                auto independent = (*instance)->uniformBufferExpected(name);
                if (!original || !independent || *original == *independent || (*instance)->uniformBlockExpected(name) ||
                    !(*aggregate)->uniformBufferExpected(name))
                {
                    return std::unexpected("Uniform array instance shares storage or accepted bytes");
                }
            }
            Gpu independent {std::move(*instance), {}};
            if (auto uploaded = update(independent, *block, resolver, *outputView); !uploaded)
            {
                return uploaded;
            }
            auto             control   = independent.bindings->uniformBlockExpected("material.controls[0]");
            auto             root      = independent.bindings->uniformBlockExpected("material");
            auto             resources = block->resolveResourcesExpected(resolver);
            const std::array bias {2.5f};
            const std::array fields {ShaderUniformWrite {"bias", std::span(bias)}};
            const std::array enabled {0u};
            const std::array rootFields {ShaderUniformWrite {"enabled", std::span(enabled)}};
            if (!control || !root || !resources || !control->setExpected(fields) || !root->setExpected(rootFields))
            {
                return std::unexpected("Prepare independent uniform array edit failed");
            }
            resources->push_back({"output", VriDescriptorType_StorageTexture, {*outputView}});
            const std::array uploads {ShaderUniformUpload {"material.controls[0]", *control},
                                      ShaderUniformUpload {"material", *root}};
            if (!independent.bindings->updateExpected(*resources, uploads))
            {
                return std::unexpected("Upload independent uniform array edit failed");
            }
            if (auto checked = render({10.5f, 5.5f, 24.f, 2.5f}, independent.bindings.get()); !checked)
            {
                return checked;
            }
            if (auto checked = render({8.5f, 5.5f, 18.5f, 0.25f}); !checked)
            {
                return checked;
            }
        }
        const std::array exposure {1.f};
        const std::array bias {1.5f};
        const std::array fields {ShaderUniformWrite {"exposure", std::span(exposure)}};
        const std::array controls {ShaderUniformWrite {"bias", std::span(bias)}};
        const std::array edits {MaterialUniformWrite {"material", fields},
                                MaterialUniformWrite {"material.controls[0]", controls}};
        const auto       before         = block->serializeExpected();
        const auto       revisionBefore = block->revision();
        const std::array missingIds {id(1)};
        const std::array invalidResources {MaterialResourceWrite {"unknown", missingIds}};
        if (block->setExpected(edits, invalidResources) || block->serializeExpected() != before ||
            block->revision() != revisionBefore)
        {
            return std::unexpected("Late invalid material resource edit changed accepted numeric values");
        }
        if (!block->setExpected(edits))
        {
            return std::unexpected("Generic material numeric edit failed");
        }
        Faults faults(gpuDevice);
        faults.rejectMap = 2;
        auto rejected    = update(*gpu, *block, resolver, *outputView);
        faults.rejectMap = 0;
        if (rejected || faults.maps != 2)
        {
            return std::unexpected("Second material uniform map failure was not reported");
        }
        if (auto retained = render({8.5f, 5.5f, 18.5f, 0.25f}); !retained)
        {
            return retained;
        }
        if (auto updated = update(*gpu, *block, resolver, *outputView); !updated)
        {
            return updated;
        }
        if (auto checked = render({19, 11, 43, 0.25f}); !checked)
        {
            return checked;
        }
        for (bool dimension : {false, true})
        {
            rejectResource = !dimension;
            wrongDimension = dimension;
            if (update(*gpu, *block, resolver, *outputView))
            {
                return std::unexpected("Invalid resource resolution was accepted");
            }
            if (auto retained = render({19, 11, 43, 0.25f}); !retained)
            {
                return retained;
            }
        }
        rejectResource          = false;
        wrongDimension          = false;
        std::string diagnostics = "retained\n";
        auto        candidate   = block->reconcileExpected(newProgram, diagnostics);
        if (!candidate)
        {
            return std::unexpected(candidate.error());
        }
        if (diagnostics == "retained\n" || candidate->resolveResourcesExpected(resolver) ||
            candidate->uniforms().size() != 4 || !candidate->uniforms().contains("material.controls[2]") ||
            candidate->resources().at("material.colors").assets.size() != 3 ||
            candidate->resources().at("material.colors").assets[0] != id(1))
        {
            return std::unexpected(
                "Changed material layout did not preserve resources or expose its new unassigned slot");
        }
        const auto* oldBasis = block->uniforms().at("material").layout().field("basis");
        const auto* newBasis = candidate->uniforms().at("material").layout().field("basis");
        if (!oldBasis || !newBasis || oldBasis->columnMajor == newBasis->columnMajor)
        {
            return std::unexpected("Material matrix-major qualification did not change actual reflection");
        }
        const std::array expanded {id(1), id(2), id(1)};
        const std::array expandedWrite {MaterialResourceWrite {"material.colors", expanded}};
        if (!candidate->setExpected({}, expandedWrite))
        {
            return std::unexpected("Assign expanded material resource array failed");
        }
        faults.rejectPipeline = true;
        auto failed           = prepare(gpuDevice, newProgram, *candidate, resolver, *outputView);
        faults.rejectPipeline = false;
        if (failed)
        {
            return std::unexpected("Rejected replacement pipeline was published");
        }
        if (auto retained = render({19, 11, 43, 0.25f}); !retained)
        {
            return retained;
        }
        auto replacement = prepare(gpuDevice, newProgram, *candidate, resolver, *outputView);
        if (!replacement)
        {
            return std::unexpected(replacement.error());
        }
        *gpu   = std::move(*replacement);
        *block = std::move(*candidate);
        if (auto migrated = render({19, 11, 43, 0.25f}); !migrated)
        {
            return migrated;
        }
        const std::array swapped {id(2), id(1), id(1)};
        const std::array swappedWrite {MaterialResourceWrite {"material.colors", swapped}};
        if (!block->setExpected({}, swappedWrite))
        {
            return std::unexpected("Edit generic material resource array failed");
        }
        if (auto updated = update(*gpu, *block, resolver, *outputView); !updated)
        {
            return updated;
        }
        if (auto changed = render({33, 11, 29, 0.25f}); !changed)
        {
            return changed;
        }
        const std::array third {0.5f};
        const std::array thirdFields {ShaderUniformWrite {"bias", std::span(third)}};
        const std::array thirdWrite {MaterialUniformWrite {"material.controls[2]", thirdFields}};
        if (!block->setExpected(thirdWrite) || !update(*gpu, *block, resolver, *outputView))
        {
            return std::unexpected("Edit grown uniform array element failed");
        }
        if (auto changed = render({35, 11, 31, 0.25f}); !changed)
        {
            return changed;
        }
        std::string shrinkDiagnostics;
        auto        shrunk = block->reconcileExpected(oldProgram, shrinkDiagnostics);
        if (!shrunk || shrunk->uniforms().size() != 3 || shrunk->uniforms().contains("material.controls[2]") ||
            shrinkDiagnostics.find("Removed material uniform: material.controls[2]") == std::string::npos)
        {
            return std::unexpected("Shrinking a uniform array did not release only its removed element");
        }
        auto smaller = prepare(gpuDevice, oldProgram, *shrunk, resolver, *outputView);
        if (!smaller)
        {
            return std::unexpected(smaller.error());
        }
        *gpu   = std::move(*smaller);
        *block = std::move(*shrunk);
        if (auto changed = render({33, 11, 29, 0.25f}); !changed)
        {
            return changed;
        }
        std::puts("MATERIAL_BLOCK_GPU pixels=527 uniform_array=2,3,2 dynamic_cb_index=1 independent_instance=1 "
                  "matrix_repack=1 resource_array=2,3 exact_restore=1 atomic_edit=1 atomic_map=1 "
                  "resolver_reject=2 pipeline_retention=1");
        return {};
    }
} // namespace

int main(int argc, char** argv)
{
    if (argc != 3 && argc != 4)
    {
        std::fprintf(stderr, "Usage: test-material-block OLD.vshaderc NEW.vshaderc [SCRATCH_DIRECTORY]\n");
        return 1;
    }
    auto oldProgram = vultra::ShaderProgram::loadExpected(argv[1]);
    auto newProgram = vultra::ShaderProgram::loadExpected(argv[2]);
    if (!oldProgram || !newProgram)
    {
        std::fprintf(stderr, "%s\n", oldProgram ? newProgram.error().c_str() : oldProgram.error().c_str());
        return 1;
    }
    const auto scratch = argc == 4 ? std::filesystem::path(argv[3]) :
                                     std::filesystem::path(argv[1]).parent_path() / "material-block-output";
    auto       result  = run(*oldProgram, *newProgram, scratch);
    if (!result)
    {
        std::fprintf(stderr, "%s\n", result.error().c_str());
        return 1;
    }
    return 0;
}
