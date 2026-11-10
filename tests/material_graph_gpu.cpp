#include <vultra/assets/material_block.hpp>
#include <vultra/assets/source_file.hpp>
#include <vultra/drivers/rhi/image_readback.hpp>
#include <vultra/drivers/rhi/shader_pipeline.hpp>
#include <vultra/platform/os/file.hpp>
#include <vultra/servers/rendering/texture_upload.hpp>

#ifdef VULTRA_WITH_SHADER_COMPILER
#include <vultra/assets/material_graph.hpp>
#endif

#include <algorithm>
#include <array>
#include <cstdio>
#include <optional>

namespace
{
    using namespace vultra;
    using Result = std::expected<void, std::string>;
    constexpr Extent kSize {19, 11};
    const std::array entries {ShaderEntry {"main", VriShaderStage_Compute}};

#ifdef VULTRA_WITH_SHADER_COMPILER
    Result write(const std::filesystem::path& path, std::string_view text)
    {
        return writeFileAtomicallyExpected(path, std::as_bytes(std::span(text)));
    }
#endif

    Result copyArtifact(const std::filesystem::path& from, const std::filesystem::path& to)
    {
        auto bytes = readSourceFileExpected(from);
        if (!bytes)
        {
            return std::unexpected(bytes.error());
        }
        return writeFileAtomicallyExpected(to, *bytes);
    }

    Result run(std::span<char*> arguments)
    {
        const bool source = arguments.size() == 3;
        if (!source && (arguments.size() != 5 || std::string_view(arguments[1]) != "--cooked"))
        {
            return std::unexpected(
                "Usage: test-material-graph-gpu GRAPH.vmatgraph SCRATCH or --cooked OLD NEW SCRATCH");
        }
        const std::filesystem::path scratch = arguments.back();
        std::error_code             error;
        std::filesystem::create_directories(scratch, error);
        if (error)
        {
            return std::unexpected(error.message());
        }
        auto programFile = scratch / (source ? "program.slang" : "program.vshaderc");
#ifdef VULTRA_WITH_SHADER_COMPILER
        std::optional<MaterialGraph> graph;
        const auto                   graphFile = std::filesystem::absolute(arguments[1], error);
        if (error)
        {
            return std::unexpected(error.message());
        }
        if (source)
        {
            auto bytes = readSourceFileExpected(graphFile);
            if (!bytes)
            {
                return std::unexpected(bytes.error());
            }
            auto decoded = MaterialGraph::parseExpected(
                std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
            if (!decoded)
            {
                return std::unexpected(decoded.error());
            }
            graph          = std::move(*decoded);
            auto generated = graph->sourceExpected(graphFile);
            if (!generated)
            {
                return std::unexpected(generated.error());
            }
            if (auto written = write(scratch / "material_graph.slangh", *generated); !written)
            {
                return written;
            }
            constexpr std::string_view shader = R"(
struct Controls
{
    float4 tint;
    float gain;
};
ParameterBlock<Controls> material;
[[vk::binding(1, 0)]] Texture2D<float4> source;
[[vk::binding(2, 0)]] SamplerState nearest;
[[vk::binding(3, 0), vk::image_format("rgba32f")]] RWTexture2D<float4> output;
#include "material_graph.slangh"
[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 pixel : SV_DispatchThreadID)
{
    uint width;
    uint height;
    output.GetDimensions(width, height);
    if (pixel.x >= width || pixel.y >= height)
    {
        return;
    }
    float2 uv = (float2(pixel.xy) + 0.5) / float2(width, height);
    output[pixel.xy] = materialPattern(material.tint, material.gain, uv, source, nearest);
}
)";
            if (auto written = write(programFile, shader); !written)
            {
                return written;
            }
        }
#else
        if (source)
        {
            return std::unexpected("Source graph checks require the host compiler");
        }
#endif
        if (!source)
        {
            if (auto copied = copyArtifact(arguments[2], programFile); !copied)
            {
                return copied;
            }
        }
        auto device = Device::create();
        if (!device)
        {
            return std::unexpected(device.error());
        }
        auto&             native = **device;
        const TextureData input {TextureFormat::eRgba8Unorm,
                                 {{{2, 1},
                                   {std::byte(255),
                                    std::byte(0),
                                    std::byte(0),
                                    std::byte(255),
                                    std::byte(0),
                                    std::byte(0),
                                    std::byte(255),
                                    std::byte(255)}}}};
        auto              texture = uploadTextureAssetExpected(native, input);
        if (!texture)
        {
            return std::unexpected(texture.error());
        }
        auto inputView = (*texture)->viewExpected();
        if (!inputView)
        {
            return std::unexpected(inputView.error());
        }
        auto desc   = colorTexture(kSize, VriFormat_RGBA32_SFLOAT);
        desc.usage  = VriTextureUsage_ShaderResourceStorage | VriTextureUsage_TransferSrc;
        auto output = Texture::create(native, desc);
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
        samplerDesc.minFilter         = VriFilter_Nearest;
        samplerDesc.magFilter         = VriFilter_Nearest;
        samplerDesc.mipmapMode        = VriMipmapMode_Nearest;
        samplerDesc.addressModeU      = VriAddressMode_ClampToEdge;
        samplerDesc.addressModeV      = VriAddressMode_ClampToEdge;
        samplerDesc.addressModeW      = VriAddressMode_ClampToEdge;
        VriDescriptor* samplerHandle  = nullptr;
        const auto     samplerStatus  = native.core.CreateSampler(native.handle, &samplerDesc, &samplerHandle);
        const auto     destroySampler = [&](VriDescriptor* descriptor)
        {
            native.core.DestroyDescriptor(descriptor);
        };
        std::unique_ptr<VriDescriptor, decltype(destroySampler)> sampler(samplerHandle, destroySampler);
        if (samplerStatus != VriResult_Success || !sampler)
        {
            return std::unexpected("Create graph texture sampler failed");
        }
        std::optional<MaterialBlock>    values;
        std::unique_ptr<ShaderBindings> bindings;
        bool                            rejectPipeline = false;
        uint32_t                        acceptedBuilds = 0;
        const std::array     resources {ShaderResourceViews {"source", VriDescriptorType_Texture, {*inputView}},
                                    ShaderResourceViews {"nearest", VriDescriptorType_Sampler, {sampler.get()}},
                                    ShaderResourceViews {"output", VriDescriptorType_StorageTexture, {*outputView}}};
        ShaderCompileOptions options;
        options.entries.assign(entries.begin(), entries.end());
        auto pipeline = ShaderPipeline::create(
            native,
            programFile,
            options,
            [&](const ShaderProgram&           program,
                std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, std::string>
            {
                if (bindings)
                {
                    if (auto valid = bindings->validateLayoutExpected(program, entries); !valid)
                    {
                        return std::unexpected(valid.error());
                    }
                }
                else
                {
                    auto block = MaterialBlock::create(program, "material");
                    if (!block)
                    {
                        return std::unexpected(block.error());
                    }
                    const std::array tint {0.25f, 0.5f, 2.f, 0.5f};
                    const std::array gain {2.f};
                    const std::array fields {
                        ShaderUniformWrite {"tint", std::span(tint), ShaderParameterKind::eVector, 1, 4},
                        ShaderUniformWrite {"gain", std::span(gain)}};
                    const std::array edits {MaterialUniformWrite {"material", fields}};
                    if (auto edited = block->setExpected(edits); !edited)
                    {
                        return std::unexpected(edited.error());
                    }
                    const std::array<std::string_view, 1> names {"material"};
                    auto owner = ShaderBindings::create(native, program, entries, names);
                    if (!owner)
                    {
                        return std::unexpected(owner.error());
                    }
                    const std::array uploads {ShaderUniformUpload {"material", block->uniforms().begin()->second}};
                    if (auto uploaded = (*owner)->updateExpected(resources, uploads); !uploaded)
                    {
                        return std::unexpected(uploaded.error());
                    }
                    bindings = std::move(*owner);
                    values   = std::move(*block);
                }
                if (rejectPipeline)
                {
                    return std::unexpected("Injected graph pipeline failure");
                }
                VriPipeline*                 handle = nullptr;
                const VriComputePipelineDesc compute {bindings->layout(), shaders.front(), native.pipelineCache};
                const auto status = native.core.CreateComputePipeline(native.handle, &compute, &handle);
                if (status != VriResult_Success || !handle)
                {
                    if (handle)
                    {
                        native.core.DestroyPipeline(handle);
                    }
                    return std::unexpected("Create graph compute pipeline failed");
                }
                if (source)
                {
                    auto saved =
                        program.saveExpected(scratch / (acceptedBuilds == 0 ? "old.vshaderc" : "new.vshaderc"));
                    if (!saved)
                    {
                        native.core.DestroyPipeline(handle);
                        return std::unexpected(saved.error());
                    }
                }
                ++acceptedBuilds;
                return handle;
            },
            scratch,
            {});
        if (!pipeline)
        {
            return std::unexpected(pipeline.error());
        }
        auto frame = Frame::create(native);
        if (!frame)
        {
            return std::unexpected(frame.error());
        }
        const auto render = [&](float factor) -> Result
        {
            auto commands = (*frame)->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            VriTextureViewDesc full {};
            full.viewType = VriTextureViewType_2D;
            full.aspect   = VriImageAspect_Color;
            if (auto transitioned = (*texture)->transitionExpected(
                    *commands,
                    {VriAccess_ShaderResourceRead, VriLayout_ShaderResource, VriPipelineStage_ComputeShader},
                    full);
                !transitioned)
            {
                return transitioned;
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
            auto buffer = bindings->uniformBufferExpected("material");
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
            if (auto bound = bindings->bindExpected(*commands, (*pipeline)->handle()); !bound)
            {
                return bound;
            }
            const VriDispatchDesc dispatch {3, 2, 1};
            native.core.CmdDispatch(*commands, &dispatch);
            if (auto submitted = (*frame)->submitAndWaitExpected(); !submitted)
            {
                return submitted;
            }
            auto image = readbackExpected(native, **output);
            if (!image)
            {
                return std::unexpected(image.error());
            }
            for (uint32_t y = 0; y < kSize.height; ++y)
            {
                for (uint32_t x = 0; x < kSize.width; ++x)
                {
                    const std::array expected {x < 9 ? 0.25f * factor : 0.f, 0.f, x < 9 ? 0.f : 2 * factor, 0.5f};
                    for (size_t c = 0; c < 4; ++c)
                    {
                        if (image->rgba[(size_t(y) * kSize.width + x) * 4 + c] != expected[c])
                        {
                            return std::unexpected(
                                "Graph material GPU output differs from independent texture/color oracle");
                        }
                    }
                }
            }
            return {};
        };
        if (auto checked = render(1); !checked)
        {
            return checked;
        }
#ifdef VULTRA_WITH_SHADER_COMPILER
        const auto originalHandle = (*pipeline)->handle();
#endif
        const auto originalGeneration = (*pipeline)->generation();
        const auto originalBuffer     = bindings->uniformBufferExpected("material");
#ifdef VULTRA_WITH_SHADER_COMPILER
        if (source)
        {
            auto invalid = *graph;
            auto shade   = std::ranges::find(invalid.nodes, "shade", &MaterialGraphNode::id);
            if (shade == invalid.nodes.end())
            {
                return std::unexpected("Fixture lost shade node");
            }
            shade->function = "dot";
            auto generated  = invalid.sourceExpected(graphFile);
            if (!generated)
            {
                return std::unexpected(generated.error());
            }
            if (auto written = write(scratch / "material_graph.slangh", *generated); !written)
            {
                return written;
            }
            if ((*pipeline)->reloadExpected() || (*pipeline)->handle() != originalHandle ||
                (*pipeline)->generation() != originalGeneration ||
                (*pipeline)->diagnostics().find(":shade") == std::string::npos)
            {
                return std::unexpected("Graph type failure did not retain its pipeline or identify the node");
            }
            if (auto retained = render(1); !retained)
            {
                return retained;
            }
            const auto before                      = readSourceFileExpected(scratch / "material_graph.slangh");
            auto       cyclic                      = *graph;
            cyclic.nodes.front().arguments.front() = cyclic.result;
            if (cyclic.sourceExpected(graphFile) || readSourceFileExpected(scratch / "material_graph.slangh") != before)
            {
                return std::unexpected("Structurally invalid graph modified its emitted source");
            }
            auto adjusted = std::ranges::find(graph->nodes, "adjusted", &MaterialGraphNode::id);
            if (adjusted == graph->nodes.end())
            {
                return std::unexpected("Fixture lost adjusted node");
            }
            adjusted->arguments.back().literal = 0.25f;
            generated                          = graph->sourceExpected(graphFile);
            if (!generated)
            {
                return std::unexpected(generated.error());
            }
            if (auto written = write(scratch / "material_graph.slangh", *generated); !written)
            {
                return written;
            }
            rejectPipeline = true;
            auto failed    = (*pipeline)->reloadExpected();
            rejectPipeline = false;
            if (failed || (*pipeline)->handle() != originalHandle || (*pipeline)->generation() != originalGeneration)
            {
                return std::unexpected("Graph pipeline failure replaced the accepted owner");
            }
            if (auto retained = render(1); !retained)
            {
                return retained;
            }
        }
#endif
        if (!source)
        {
            if (auto copied = copyArtifact(arguments[3], programFile); !copied)
            {
                return copied;
            }
        }
        if (auto replaced = (*pipeline)->reloadExpected(); !replaced)
        {
            return std::unexpected(replaced.error());
        }
        if ((*pipeline)->generation() != originalGeneration + 1 ||
            bindings->uniformBufferExpected("material") != originalBuffer)
        {
            return std::unexpected("Graph reload lost generation or stable parameter storage");
        }
        if (auto checked = render(0.5f); !checked)
        {
            return checked;
        }
        const auto       updatedPipeline = (*pipeline)->handle();
        const std::array gain {4.f};
        const std::array fields {ShaderUniformWrite {"gain", std::span(gain)}};
        const std::array edits {MaterialUniformWrite {"material", fields}};
        if (auto changed = values->setExpected(edits); !changed)
        {
            return std::unexpected(changed.error());
        }
        const std::array uploads {ShaderUniformUpload {"material", values->uniforms().begin()->second}};
        if (auto uploaded = bindings->updateExpected(resources, uploads); !uploaded)
        {
            return uploaded;
        }
        if ((*pipeline)->handle() != updatedPipeline || bindings->uniformBufferExpected("material") != originalBuffer)
        {
            return std::unexpected("Numeric graph material edit rebuilt pipeline/storage");
        }
        if (auto checked = render(1); !checked)
        {
            return checked;
        }
        std::printf("MATERIAL_GRAPH_GPU pixels=209 struct_outputs=2 numeric_reuse=1 source_reload=%d\n", source);
        return {};
    }
} // namespace

int main(int argc, char** argv)
{
    if (auto checked = run(std::span(argv, size_t(argc))); !checked)
    {
        std::fprintf(stderr, "%s\n", checked.error().c_str());
        return 1;
    }
    return 0;
}
