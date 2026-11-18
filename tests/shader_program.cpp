#include "../examples/common/triangle.hpp"
#include "support/check.hpp"
#include "support/cleared_texture.hpp"

#include <vultra/gpu/buffer_upload.hpp>
#include <vultra/gpu/image_readback.hpp>
#include <vultra/graph/render_graph.hpp>
#include <vultra/platform/os/file.hpp>

#include <nlohmann/json.hpp>
#include <xxhash.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <set>
#include <thread>
#include <tuple>

namespace
{
    using support::must;
    using support::require;

    constexpr std::string_view kRejected = "Expected shader input rejection";

    std::vector<uint8_t> readBytes(const std::filesystem::path& file)
    {
        std::ifstream input(file, std::ios::binary);
        require(bool(input), "Open test shader artifact");
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    // Alter a valid artifact's metadata while retaining its checksum to exercise schema checks.
    template<class Callback>
    void alterDocument(const std::filesystem::path& file, const std::vector<uint8_t>& original, Callback&& callback)
    {
        auto document = nlohmann::json::from_cbor(original.begin() + 16, original.end(), true, false);
        require(!document.is_discarded(), "Shader artifact payload is not CBOR");
        callback(document);
        const auto           payload = nlohmann::json::to_cbor(document);
        std::vector<uint8_t> bytes(original.begin(), original.begin() + 16);
        const auto           hash = XXH3_64bits(payload.data(), payload.size());
        for (size_t i = 0; i < sizeof(hash); ++i)
        {
            bytes[8 + i] = uint8_t(hash >> (8 * i));
        }
        bytes.insert(bytes.end(), payload.begin(), payload.end());
        must(vultra::writeFileAtomically(file, std::as_bytes(std::span(bytes))));
        support::rejects(vultra::ShaderProgram::load(file), kRejected);
    }

    void resourceViews(const std::filesystem::path& root)
    {
        using namespace vultra;
        const auto program = must(ShaderProgram::compileSource(root / "resource_views.slang", R"(
Texture1D<float4> line;
Texture1DArray<float4> lines;
Texture2D<float4> plane;
Texture2DArray<float4> planes;
Texture3D<float4> volume;
TextureCube<float4> cube;
TextureCubeArray<float4> cubes;
SamplerState filter;
RWStructuredBuffer<float4> output;
[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain()
{
    output[0] = line.SampleLevel(filter, 0.5, 0) + lines.SampleLevel(filter, float2(0.5, 0), 0) +
                plane.SampleLevel(filter, float2(0.5), 0) + planes.SampleLevel(filter, float3(0.5, 0.5, 0), 0) +
                volume.SampleLevel(filter, float3(0.5), 0) + cube.SampleLevel(filter, float3(0, 0, 1), 0) +
                cubes.SampleLevel(filter, float4(0, 0, 1, 0), 0);
}
)"));
        const auto file    = root / "resource_views.vshaderc";
        must(program.save(file));
        const auto       restored = must(ShaderProgram::load(file));
        const std::array expected {std::pair {"line", VriTextureViewType_1D},
                                   std::pair {"lines", VriTextureViewType_1DArray},
                                   std::pair {"plane", VriTextureViewType_2D},
                                   std::pair {"planes", VriTextureViewType_2DArray},
                                   std::pair {"volume", VriTextureViewType_3D},
                                   std::pair {"cube", VriTextureViewType_Cube},
                                   std::pair {"cubes", VriTextureViewType_CubeArray}};
        for (const auto* candidate : {&program, &restored})
        {
            const auto bindings = must(candidate->resourceBindings());
            for (const auto& [name, view] : expected)
            {
                const auto binding = std::ranges::find(bindings, name, &ShaderResourceBinding::name);
                require(binding != bindings.end() && binding->type == VriDescriptorType_Texture &&
                            binding->viewType == view,
                        "Compiler/cooked reflection lost a VRI texture dimension");
            }
        }
    }

    // Bindings as each target sees them: D3D registers per class (b, t, u, s), descriptor slots elsewhere, and push
    // constants told apart on every target. One archive carries the program of every target of this build; each
    // keeps its reflection, and a target it lacks is an error naming the ones it has.
    void deviceBindings(const std::filesystem::path& root)
    {
        using namespace vultra;
        std::vector<ShaderProgram> programs;
        for (const auto& profile : buildShaderProfiles())
        {
            ShaderCompileOptions options;
            options.profile = profile;
            programs.push_back(must(ShaderProgram::compileSource(root / "device_bindings.slang",
                                                                 R"(
struct Parameters
{
    float gain;
};
[[vk::push_constant]] ConstantBuffer<Parameters> parameters;
ConstantBuffer<Parameters> constants;
Texture2D<float4> image;
StructuredBuffer<float4> elements;
[format("rgba16f")] RWTexture2D<float4> written;
RWStructuredBuffer<float4> output;
SamplerState filter;
[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain()
{
    written[uint2(0, 0)] = image.SampleLevel(filter, float2(0.5), 0) * elements[0] * constants.gain;
    output[0]            = float4(parameters.gain);
}
)",
                                                                 options)));
        }
        const auto file = root / "device_bindings.vshaderc";
        must(ShaderProgram::save(file, programs));
        support::rejects(ShaderProgram::load(file, "no_such_target"), kRejected);
        // Name, slot on D3D, slot elsewhere.
        const std::array expected {std::tuple {"constants", 1u, 0u},
                                   std::tuple {"image", 0u, 1u},
                                   std::tuple {"elements", 1u, 2u},
                                   std::tuple {"written", 0u, 3u},
                                   std::tuple {"output", 1u, 4u},
                                   std::tuple {"filter", 0u, 5u}};
        for (const auto& program : programs)
        {
            const auto restored = must(ShaderProgram::load(file, program.profile));
            const bool d3d      = isD3DProfile(program.profile);
            for (const auto* candidate : {&program, &restored})
            {
                const auto bindings = must(candidate->resourceBindings());
                const auto push     = std::ranges::find(bindings, "parameters", &ShaderResourceBinding::name);
                require(push != bindings.end() && push->pushConstant && push->set == 0 && push->binding == 0,
                        "Reflection lost the push constants or their register");
                for (const auto& [name, d3dSlot, slot] : expected)
                {
                    const auto binding = std::ranges::find(bindings, name, &ShaderResourceBinding::name);
                    require(binding != bindings.end() && !binding->pushConstant && binding->set == 0 &&
                                binding->binding == (d3d ? d3dSlot : slot),
                            std::string("Reflection placed ") + name + " at another register or slot");
                }
            }
        }
    }

    // shaderSetLayout places what a caller binds where each API looks for it. With `image` (t0) left unbound and
    // `elements` (t1) bound, D3D12's range-order numbering needs a range for image too; other APIs bind each range at
    // its slot and leave it out. A storage buffer declared before a storage texture cannot be laid out for D3D12.
    void setLayouts(const std::filesystem::path& root)
    {
        using namespace vultra;
        for (const auto api : Device::graphicsAPIs())
        {
            ShaderCompileOptions options;
            options.profile            = shaderProfile(api);
            const auto        program  = must(ShaderProgram::compileSource(root / "set_layouts.slang",
                                                                   R"(
Texture2D<float4> image;
StructuredBuffer<float4> elements;
RWStructuredBuffer<float4> output;
[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain()
{
    output[0] = elements[0] + image.Load(int3(0, 0, 0));
}
)",
                                                                   options));
            const auto        bindings = must(program.resourceBindings());
            std::vector<bool> bound;
            for (const auto& binding : bindings)
            {
                bound.push_back(binding.name != "image");
            }
            const auto layout = must(shaderSetLayout(api, bindings, bound, VriShaderStage_Compute));
            const auto holds  = [&](std::string_view name)
            {
                return std::ranges::any_of(layout.bindings,
                                           [&](size_t index)
                                           {
                                               return bindings[index].name == name;
                                           });
            };
            require(layout.ranges.size() == layout.bindings.size() && holds("elements") && holds("output") &&
                        holds("image") == (api == VriGraphicsAPI_D3D12),
                    "shaderSetLayout laid out other ranges than the API needs");

            const auto misordered = must(ShaderProgram::compileSource(root / "misordered.slang",
                                                                      R"(
RWStructuredBuffer<float4> output;
[format("rgba16f")] RWTexture2D<float4> image;
[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain()
{
    image[uint2(0, 0)] = output[0];
}
)",
                                                                      options));
            const auto declared   = must(misordered.resourceBindings());
            const auto result     = shaderSetLayout(api, declared, std::vector<bool>(declared.size(), true), 0);
            require(result.has_value() != (api == VriGraphicsAPI_D3D12),
                    "A storage buffer before a storage texture must fail on D3D12 only");
        }
    }

    void userAttributes(const std::filesystem::path& root)
    {
        using namespace vultra;
        const auto program = must(ShaderProgram::compileSource(root / "attributes.slang", R"(
[__AttributeUsage(_AttributeTargets.Var)]
struct PassInputAttribute { string resource; };
[__AttributeUsage(_AttributeTargets.Var)]
struct PassParamAttribute { float minimum; float maximum; string label; };
[__AttributeUsage(_AttributeTargets.Function)]
struct PassStageAttribute { int order; };

[PassInput("scene.hdr")] Texture2D<float4> source;
RWTexture2D<float4> result;
struct Parameters
{
    [PassParam(0, 8.5, "Gain")] float gain = 1.5;
    [PassParam(1, 21, "Width")] int width = 5;
};
ConstantBuffer<Parameters> parameters;

[PassStage(2)]
[shader("compute")]
[numthreads(8, 4, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
    result[id.xy] = source[id.xy] * parameters.gain * parameters.width;
}
)"));
        const auto file    = root / "attributes.vshaderc";
        must(program.save(file));
        const auto restored = must(ShaderProgram::load(file));
        for (const auto* candidate : {&program, &restored})
        {
            const auto* input = candidate->parameters.field("source");
            const auto* block = candidate->parameters.field("parameters");
            const auto* gain  = block ? block->field("gain") : nullptr;
            const auto* width = block ? block->field("width") : nullptr;
            require(input && gain && width && candidate->parameters.field("result"),
                    "Reflection lost an attributed global parameter");
            const auto* resource = input->attribute("PassInput");
            require(resource && resource->arguments.size() == 1 &&
                        resource->arguments[0].kind == ShaderAttributeArgument::Kind::eString &&
                        resource->arguments[0].text == "scene.hdr",
                    "String attribute argument was not reflected without its quotes");
            require(candidate->parameters.field("result")->attributes.empty(),
                    "An unattributed parameter reported attributes");
            const auto* range = gain->attribute("PassParam");
            require(range && range->arguments.size() == 3 &&
                        range->arguments[0].kind == ShaderAttributeArgument::Kind::eFloat &&
                        range->arguments[0].number == 0 && range->arguments[1].number == 8.5 &&
                        range->arguments[2].text == "Gain",
                    "Float attribute arguments were not reflected, including an integer literal");
            require(gain->defaultValue && *gain->defaultValue == 1.5 && width->defaultValue &&
                        *width->defaultValue == 5 && !input->defaultValue,
                    "Scalar parameter initializers were not reflected");
            const auto& shader = candidate->shaders.at(0);
            const auto* stage  = shader.attribute("PassStage");
            require(stage && stage->arguments.size() == 1 &&
                        stage->arguments[0].kind == ShaderAttributeArgument::Kind::eInteger &&
                        stage->arguments[0].number == 2 && !shader.attribute("PassParam"),
                    "Entry-point attribute was not reflected");
            require(shader.threadGroup == std::array<uint32_t, 3> {8, 4, 1}, "Thread group size changed");
        }
    }

    // Debug information and optimization are chosen per compile, not by the C++ build mode.
    void compileModes(const std::filesystem::path& root)
    {
        using namespace vultra;
        const char* source  = R"(
RWStructuredBuffer<float> values;
float weight(int index)
{
    return float(index) * 0.5;
}
// vultra-debug-marker
[shader("compute")] [numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    float sum = 0;
    for (int index = 0; index < 4; ++index)
    {
        sum += weight(index);
    }
    values[id.x] = sum;
}
)";
        const auto  compile = [&](bool debugInfo, bool optimize)
        {
            ShaderCompileOptions options;
            options.debugInfo = debugInfo;
            options.optimize  = optimize;
            return must(ShaderProgram::compileSource(root / "modes.slang", source, options));
        };
        const auto contains = [](const ShaderProgram& program, std::string_view text)
        {
            const auto&            words = program.shaders.at(0).words;
            const std::string_view bytes(reinterpret_cast<const char*>(words.data()), words.size() * sizeof(uint32_t));
            return bytes.find(text) != std::string_view::npos;
        };
        const auto size = [](const ShaderProgram& program)
        {
            return program.shaders.at(0).words.size();
        };
        const auto plain             = compile(false, false);
        const auto debugged          = compile(true, false);
        const auto optimized         = compile(false, true);
        const auto optimizedDebugged = compile(true, true);
        require(!contains(plain, "vultra-debug-marker") && !contains(plain, "NonSemantic.Shader.DebugInfo"),
                "A shader compiled without debug information carries its source");
        require(contains(debugged, "NonSemantic.Shader.DebugInfo.100") && contains(debugged, "vultra-debug-marker"),
                "Debug information does not embed the shader source for GPU debuggers");
        require(plain.compileKey != debugged.compileKey && plain.compileKey != optimized.compileKey &&
                    debugged.compileKey != optimizedDebugged.compileKey,
                "Compile modes share a cache key");
        std::cout << "SPIR-V words: plain " << size(plain) << ", optimized " << size(optimized) << ", debug "
                  << size(debugged) << ", optimized debug " << size(optimizedDebugged) << '\n';
        require(size(optimized) < size(plain), "Optimization did not shrink a shader with a constant loop");
        require(contains(optimizedDebugged, "vultra-debug-marker") && size(optimizedDebugged) < size(debugged),
                "An optimized shader with debug information lost its source or was not optimized");
    }

    // An edited source file reaches a live pipeline through the file watcher; a broken edit keeps the last one.
    void watchedReload(const std::filesystem::path& root)
    {
        using namespace vultra;
        using namespace std::chrono_literals;
        const auto directory = root / "watched";
        std::filesystem::create_directories(directory);
        const auto file  = directory / "kernel.slang";
        const auto write = [&](const char* body)
        {
            std::ofstream(file, std::ios::trunc)
                << "[shader(\"compute\")] [numthreads(1, 1, 1)] void main() { " << body << " }\n";
        };
        write("");
        const auto                  device = must(Device::create());
        VriPipelineLayout*          layout = nullptr;
        const VriPipelineLayoutDesc layoutDesc {};
        must(check(device->core.CreatePipelineLayout(device->handle, &layoutDesc, &layout), "Watched reload layout"));
        {
            const auto watched  = must(ShaderPipeline::create(
                *device,
                file,
                std::vector<ShaderEntry> {{"main", VriShaderStage_Compute}},
                [&](std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, Error>
                {
                    VriPipeline*                 result = nullptr;
                    const VriComputePipelineDesc desc {layout, shaders[0], device->pipelineCache};
                    if (auto pipeline = check(device->core.CreateComputePipeline(device->handle, &desc, &result),
                                              "Watched reload pipeline");
                        !pipeline)
                    {
                        return std::unexpected(pipeline.error());
                    }
                    return result;
                }));
            auto&      pipeline = *watched;
            // Polls like a frame loop until the pipeline reacts to the edit or the time is up.
            const auto pollUntil = [&](auto&& done)
            {
                const auto deadline = std::chrono::steady_clock::now() + 5s;
                while (std::chrono::steady_clock::now() < deadline)
                {
                    const bool reloaded = pipeline.poll();
                    if (done(reloaded))
                    {
                        return true;
                    }
                    std::this_thread::sleep_for(16ms);
                }
                return false;
            };
            const auto generation = pipeline.generation();
            require(!pipeline.poll(), "An unedited shader reloaded");
            write("int edited = 1;");
            require(pollUntil(
                        [](bool reloaded)
                        {
                            return reloaded;
                        }) &&
                        pipeline.generation() == generation + 1 && pipeline.diagnostics().empty(),
                    "An edited shader file did not reload its pipeline");
            auto* handle = pipeline.handle();
            write("this is not slang;");
            require(pollUntil(
                        [&](bool)
                        {
                            return !pipeline.diagnostics().empty();
                        }) &&
                        pipeline.handle() == handle && pipeline.generation() == generation + 1,
                    "A broken shader edit replaced the last valid pipeline");
            write("int repaired = 2;");
            require(pollUntil(
                        [](bool reloaded)
                        {
                            return reloaded;
                        }) &&
                        pipeline.generation() == generation + 2 && pipeline.diagnostics().empty(),
                    "A repaired shader file did not recover");
        }
        device->core.DestroyPipelineLayout(layout);
    }

    // Keywords: a program with a bool and a three-valued enum keyword has six variants. Each computes, from the
    // macros in its source and in a module it imports, a number that only its keyword values give; the variants
    // compile to distinct keys, a cooked archive holds all six and selects each, and an unknown keyword or value is
    // refused with the choices.
    void keywordVariants(const std::filesystem::path& root)
    {
        using namespace vultra;
        const auto directory = root / "keywords";
        std::filesystem::create_directories(directory);
        std::ofstream(directory / "keyword_module.slang", std::ios::trunc) << R"(module keyword_module;
public uint moduleFlag()
{
#if FLAG
    return 100;
#else
    return 0;
#endif
}
)";
        const auto source = directory / "keywords.slang";
        std::ofstream(source, std::ios::trunc) << R"(import vultra.keywords;
import keyword_module;
[VultraKeyword("FLAG", "bool")]
[VultraKeyword("MODE", "enum:low,mid,high")]
void vultraKeywords() {}
RWStructuredBuffer<uint> result;
[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
#if MODE == 2
    const uint mode = 2;
#elif MODE == 1
    const uint mode = 1;
#else
    const uint mode = 0;
#endif
#if FLAG
    const uint flag = 10;
#else
    const uint flag = 0;
#endif
    result[0] = flag + mode + moduleFlag();
}
)";
        const auto           device = must(Device::create());
        ShaderCompileOptions options;
        options.includeDirectories = {"builtin/shaders", directory};
        options.profile            = shaderProfile(device->graphicsAPI());
        const auto variants        = must(ShaderProgram::compileVariants(source, options));
        require(variants.size() == 6 && variants.front().keywords.size() == 2 &&
                    variants.front().keywords[1].values == std::vector<std::string> {"low", "mid", "high"},
                "Keyword declarations or their combinations were not reflected");
        std::set<std::string> keys;
        for (const auto& variant : variants)
        {
            keys.insert(variant.compileKey);
        }
        require(keys.size() == variants.size(), "Two keyword variants share a compile key");

        // Each variant computes its own number on the GPU.
        const auto run = [&](const ShaderProgram& program) -> uint32_t
        {
            const auto                 bindings = must(program.resourceBindings());
            const auto                 laid     = must(shaderSetLayout(device->graphicsAPI(),
                                                   bindings,
                                                   std::vector<bool>(bindings.size(), true),
                                                   VriShaderStage_Compute));
            const VriDescriptorSetDesc set {0, laid.ranges.data(), uint32_t(laid.ranges.size())};
            VriPipelineLayoutDesc      layoutDesc {};
            layoutDesc.descriptorSets   = &set;
            layoutDesc.descriptorSetNum = 1;
            layoutDesc.shaderStages     = VriShaderStage_Compute;
            VriPipelineLayout* layout   = nullptr;
            must(check(device->core.CreatePipelineLayout(device->handle, &layoutDesc, &layout), "Keyword layout"));
            auto                  pipeline = must(ShaderPipeline::create(
                *device,
                program,
                {{"main", VriShaderStage_Compute}},
                [&](std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, Error>
                {
                    VriComputePipelineDesc desc {};
                    desc.pipelineLayout  = layout;
                    desc.shader          = shaders.front();
                    VriPipeline* created = nullptr;
                    if (auto made = check(device->core.CreateComputePipeline(device->handle, &desc, &created),
                                          "Keyword pipeline");
                        !made)
                    {
                        return std::unexpected(made.error());
                    }
                    return created;
                }));
            VriDescriptorPoolDesc poolDesc {};
            poolDesc.descriptorSetMaxNum = 1;
            poolDesc.storageBufferMaxNum = 1;
            VriDescriptorPool* pool      = nullptr;
            must(check(device->core.CreateDescriptorPool(device->handle, &poolDesc, &pool), "Keyword pool"));
            VriDescriptorSet* descriptors = nullptr;
            must(check(device->core.AllocateDescriptorSets(pool, layout, 0, &descriptors, 1), "Keyword set"));
            auto                    buffer = must(Buffer::create(*device,
                                                                 {sizeof(uint32_t),
                                                                  sizeof(uint32_t),
                                                                  VriBufferUsage_StorageBuffer | VriBufferUsage_TransferSrc,
                                                                  VriMemoryLocation_Device}));
            const VriBufferViewDesc viewDesc {buffer.handle,
                                              VriDescriptorType_StorageBuffer,
                                              VriFormat_Unknown,
                                              0,
                                              sizeof(uint32_t)};
            VriDescriptor*          view = nullptr;
            must(check(device->core.CreateBufferView(device->handle, &viewDesc, &view), "Keyword view"));
            const VriDescriptor*               descriptor = view;
            const VriDescriptorRangeUpdateDesc update {&descriptor, 1, 0};
            device->core.UpdateDescriptorRanges(descriptors, 0, 1, &update);
            RenderGraph graph(*device);
            const auto  resource = must(graph.importResource("result", buffer));
            must(graph.addPass(
                "Keywords",
                {{resource, Usage::eStorageWrite, true}},
                [&](VriCommandBuffer* cmd, RenderGraph&) -> std::expected<void, Error>
                {
                    device->core.CmdSetPipelineLayout(cmd, layout);
                    device->core.CmdSetPipeline(cmd, pipeline->handle());
                    device->core.CmdSetDescriptorSet(cmd, 0, descriptors);
                    const VriDispatchDesc one {1, 1, 1};
                    device->core.CmdDispatch(cmd, &one);
                    return {};
                },
                true));
            must(graph.compile());
            const auto frame = must(Frame::create(*device));
            must(graph.execute(must(frame->begin())));
            must(frame->submitAndWait());
            const auto bytes = must(readbackBuffer(*device, buffer));
            uint32_t   value = 0;
            std::memcpy(&value, bytes.data(), sizeof(value));
            device->waitIdleForDestruction();
            device->core.DestroyDescriptor(view);
            device->core.DestroyDescriptorPool(pool);
            pipeline.reset();
            device->core.DestroyPipelineLayout(layout);
            return value;
        };
        for (const auto& variant : variants)
        {
            const uint32_t flag     = variant.keywordValues[0].value == "1" ? 110 : 0;
            const auto&    mode     = variant.keywordValues[1].value;
            const uint32_t modeTerm = mode == "high" ? 2 : (mode == "mid" ? 1 : 0);
            require(run(variant) == flag + modeTerm, "A keyword variant did not compile with its macros");
        }

        // Selecting one variant from source and from a cooked archive gives the same program.
        auto chosen       = options;
        chosen.keywords   = {{"MODE", "high"}};
        const auto single = must(ShaderProgram::compile(source, chosen));
        require(single.keywordValues.size() == 2 && single.keywordValues[0].value == "0" &&
                    single.keywordValues[1].value == "high" && single.compileKey == variants[4].compileKey,
                "Compiling a keyword selection did not give its variant");
        const auto cooked = directory / "keywords.vshaderc";
        require(must(ShaderProgram::cook(source, cooked, options)), "Keyword variants were not cooked");
        require(!must(ShaderProgram::cook(source, cooked, options)), "An unchanged keyword archive was cooked again");
        require(must(ShaderProgram::loadTargets(cooked)).size() == 6, "The archive does not hold every variant");
        const auto loaded = must(ShaderProgram::load(cooked, options.profile, chosen.keywords));
        require(loaded.compileKey == single.compileKey && run(loaded) == 2,
                "The cooked archive did not select the keyword variant");
        require(must(ShaderProgram::load(cooked, options.profile)).compileKey == variants[0].compileKey,
                "The cooked archive's default variant is not the first");
        support::rejects(ShaderProgram::load(cooked, options.profile, std::vector<ShaderDefine> {{"SHADOWS", "1"}}),
                         "An unknown keyword was accepted");
        support::rejects(ShaderProgram::load(cooked, options.profile, std::vector<ShaderDefine> {{"MODE", "ultra"}}),
                         "An unknown keyword value was accepted");
        support::rejects(ShaderProgram::load(cooked), "An archive of variants loaded as one program");
        auto unknown     = options;
        unknown.keywords = {{"FLAG", "2"}};
        support::rejects(ShaderProgram::compile(source, unknown), "A keyword value the source does not list compiled");
    }
} // namespace

int main()
{
    using namespace vultra;
    namespace fs    = std::filesystem;
    const auto root = fs::path("build/.tmp/shader-program") /
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    fs::create_directories(root / "source");
    resourceViews(root);
    deviceBindings(root);
    setLayouts(root);
    userAttributes(root);
    compileModes(root);
    watchedReload(root);
    keywordVariants(root);
    const auto source = root / "source/triangle.slang";
    const auto cooked = root / "source/triangle.vshaderc";
    fs::copy_file("examples/offline/shaders/triangle.slang", source);
    fs::copy_file("examples/offline/shaders/color.slangh", root / "source/color.slangh");
    const std::array includes {fs::path("builtin/shaders"), fs::path("examples/common")};
    const std::array entries {ShaderEntry {"vertexMain", VriShaderStage_Vertex},
                              ShaderEntry {"fragmentMain", VriShaderStage_Fragment}};
    const auto       program = must(ShaderProgram::compile(source, {}, includes));
    require(program.shaders.size() == 2 && !program.rayQuery, "Shader entry discovery failed");
    must(program.save(cooked));
    ShaderCompileOptions cacheOptions;
    cacheOptions.includeDirectories.assign(includes.begin(), includes.end());
    require(!must(ShaderProgram::cook(source, cooked, cacheOptions)), "Raw Slang unchanged cache missed");
    auto defineOptions = cacheOptions;
    defineOptions.defines.push_back({"TEST_CACHE_DEFINE", "1"});
    require(must(ShaderProgram::cook(source, cooked, defineOptions)) &&
                !must(ShaderProgram::cook(source, cooked, defineOptions)),
            "Macro options did not invalidate/cache raw Slang");
    must(program.save(cooked));
    const auto original   = readBytes(cooked);
    const auto restored   = must(ShaderProgram::load(cooked));
    const auto fromMemory = must(ShaderProgram::load(std::as_bytes(std::span(original)), "embedded/triangle.vshaderc"));
    require(fromMemory.shaders.size() == restored.shaders.size() &&
                fromMemory.shaders[0].words == restored.shaders[0].words &&
                fromMemory.shaders[1].words == restored.shaders[1].words &&
                fromMemory.compileKey == restored.compileKey,
            "Embedded shader archive differs from filesystem loading");
    auto damaged = original;
    damaged.back() ^= 1;
    support::rejects(ShaderProgram::load(std::as_bytes(std::span(damaged)), "embedded/damaged.vshaderc"), kRejected);
    support::rejects(ShaderProgram::load(std::as_bytes(std::span(original)).first(15), "embedded/truncated.vshaderc"),
                     kRejected);
    require(restored.shaders.size() == program.shaders.size() &&
                restored.shaders[0].words == program.shaders[0].words &&
                restored.shaders[1].words == program.shaders[1].words,
            "Cooked bytecode changed during roundtrip");
    const auto views = must(restored.descriptors(entries));
    require(views.size() == 2 && std::string_view(views[0].entryPointName) == "vertexMain" &&
                views[1].stage == VriShaderStage_Fragment,
            "Cooked descriptor selection failed");

    const std::string_view raySource = R"(
struct Payload
{
    float value;
};
[shader("raygeneration")]
void raygenMain()
{
}
[shader("miss")]
void missMain(inout Payload payload)
{
    payload.value = 0;
}
[shader("closesthit")]
void closestHitMain(inout Payload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    payload.value = attributes.barycentrics.x;
}
[shader("anyhit")]
void anyHitMain(inout Payload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    payload.value = attributes.barycentrics.y;
}
[shader("intersection")]
void intersectionMain()
{
    BuiltInTriangleIntersectionAttributes attributes;
    attributes.barycentrics = 0;
    ReportHit(1, 0, attributes);
}
[shader("callable")]
void callableMain(inout Payload payload)
{
    payload.value = 1;
}
)";
    const std::array       rayEntries {ShaderEntry {"raygenMain", VriShaderStage_RayGen},
                                 ShaderEntry {"missMain", VriShaderStage_Miss},
                                 ShaderEntry {"closestHitMain", VriShaderStage_ClosestHit},
                                 ShaderEntry {"anyHitMain", VriShaderStage_AnyHit},
                                 ShaderEntry {"intersectionMain", VriShaderStage_Intersection},
                                 ShaderEntry {"callableMain", VriShaderStage_Callable}};
    const auto             rayProgram = must(ShaderProgram::compileSource(root / "ray.slang", raySource));
    require(rayProgram.shaders.size() == rayEntries.size(), "Ray tracing entry discovery failed");
    const auto rayCooked = root / "ray.vshaderc";
    must(rayProgram.save(rayCooked));
    const auto           rayRestored = must(ShaderProgram::load(rayCooked));
    const auto           rayViews    = must(rayRestored.descriptors(rayEntries));
    ShaderCompileOptions rayOptions;
    rayOptions.entries.assign(rayEntries.begin(), rayEntries.end());
    const auto rayRequested = must(ShaderProgram::compileSource(root / "ray.slang", raySource, rayOptions));
    for (size_t i = 0; i < rayEntries.size(); ++i)
    {
        require(rayViews[i].stage == rayEntries[i].stage &&
                    std::string_view(rayViews[i].entryPointName) == rayEntries[i].name &&
                    rayRestored.shaders[i].words == rayProgram.shaders[i].words &&
                    rayRequested.shaders[i].entry.stage == rayEntries[i].stage,
                "Ray tracing explicit selection or source-free roundtrip lost stage/bytecode");
    }
    alterDocument(rayCooked,
                  readBytes(rayCooked),
                  [](auto& document)
                  {
                      document["programs"][0]["entries"][0]["stage"] =
                          uint32_t(VriShaderStage_RayGen | VriShaderStage_Miss);
                  });
    const std::array wrongStage {ShaderEntry {"vertexMain", VriShaderStage_Fragment}};
    support::rejects(ShaderProgram::compile(source, wrongStage, includes), kRejected);
    support::rejects(restored.descriptors(wrongStage), kRejected);
    const std::array missing {ShaderEntry {"missing", VriShaderStage_Vertex}};
    support::rejects(restored.descriptors(missing), kRejected);
    alterDocument(cooked,
                  original,
                  [](auto& document)
                  {
                      document["version"] = 2;
                  });
    alterDocument(cooked,
                  original,
                  [](auto& document)
                  {
                      document["programs"][0]["target"] = "dxil";
                  });
    alterDocument(cooked,
                  original,
                  [](auto& document)
                  {
                      document["programs"][0]["entries"][0]["stage"] = uint64_t(0x100000001);
                  });
    alterDocument(cooked,
                  original,
                  [](auto& document)
                  {
                      document["programs"][0]["entries"][1]["name"] = "vertexMain";
                  });
    alterDocument(cooked,
                  original,
                  [](auto& document)
                  {
                      document["programs"][0]["entries"][0]["code"] = nlohmann::json::binary({1, 2, 3});
                  });
    alterDocument(cooked,
                  original,
                  [](auto& document)
                  {
                      document["programs"][0]["parameters"]["descriptor_type"] = 99;
                  });
    alterDocument(cooked,
                  original,
                  [](auto& document)
                  {
                      document["programs"][0]["parameters"]["view_type"] = 99;
                  });
    alterDocument(cooked,
                  original,
                  [](auto& document)
                  {
                      document["programs"].push_back(document["programs"][0]);
                  });
    alterDocument(cooked,
                  original,
                  [](auto& document)
                  {
                      document["programs"] = nlohmann::json::array();
                  });
    must(writeFileAtomically(cooked, std::as_bytes(std::span(original))));
    auto invalid                = program;
    invalid.shaders[0].words[0] = 0;
    support::rejects(invalid.save(cooked), kRejected);
    require(readBytes(cooked) == original, "Rejected shader save overwrote last good artifact");

    const auto           device = must(Device::create());
    ShaderCompileOptions wgslOptions;
    wgslOptions.profile = "wgsl";
    wgslOptions.includeDirectories.assign(includes.begin(), includes.end());
    const auto wgsl = must(ShaderProgram::compile(source, wgslOptions));
    require(wgsl.profile == "wgsl" && wgsl.compileKey != program.compileKey && wgsl.shaders.size() == 2,
            "WGSL target shared the SPIR-V compiler identity or lost shader entries");
    for (const auto& shader : wgsl.shaders)
    {
        require(shader.words.empty() && !shader.wgsl.empty(), "WGSL program retained the wrong target payload");
    }
    const auto wgslFile = root / "triangle-wgsl.vshaderc";
    must(wgsl.save(wgslFile));
    const auto wgslBytes = readBytes(wgslFile);
    const auto wgslRestored =
        must(ShaderProgram::load(std::as_bytes(std::span(wgslBytes)), "embedded/triangle-wgsl.vshaderc"));
    require(wgslRestored.profile == "wgsl" && wgslRestored.shaders[0].wgsl == wgsl.shaders[0].wgsl &&
                wgslRestored.shaders[1].wgsl == wgsl.shaders[1].wgsl,
            "Cooked WGSL changed during embedded archive loading");
    const auto wgslDescriptors = must(wgslRestored.descriptors(entries));
    require(std::string_view(static_cast<const char*>(wgslDescriptors[0].bytecode)) == wgsl.shaders[0].wgsl &&
                wgslDescriptors[0].bytecodeSize == wgsl.shaders[0].wgsl.size() + 1,
            "VRI did not receive owned, NUL-terminated WGSL");
    alterDocument(wgslFile,
                  wgslBytes,
                  [](auto& document)
                  {
                      document["programs"][0]["entries"][0]["code"].get_binary().push_back(0);
                  });
    auto invalidWgsl             = wgsl;
    invalidWgsl.shaders[0].words = program.shaders[0].words;
    support::rejects(invalidWgsl.save(wgslFile), kRejected);
    wgslOptions.rayQuery = true;
    support::rejects(ShaderProgram::compile(source, wgslOptions), kRejected);
    // The GPU half runs the cook this build's device consumes; a cook for another target must stop before the
    // builder.
    const auto           deviceProfile = shaderProfile(device->graphicsAPI());
    const ShaderProgram& mismatched    = deviceProfile == "wgsl" ? restored : wgslRestored;
    ShaderProgram        deviceProgram = deviceProfile == "wgsl" ? wgsl : program;
    if (isD3DProfile(deviceProfile))
    {
        ShaderCompileOptions deviceOptions;
        deviceOptions.profile = deviceProfile;
        deviceOptions.includeDirectories.assign(includes.begin(), includes.end());
        deviceProgram = must(ShaderProgram::compile(source, deviceOptions));
    }
    must(deviceProgram.save(cooked));
    const auto deviceCook    = readBytes(cooked);
    bool       builderCalled = false;
    support::rejects(ShaderPipeline::create(*device,
                                            mismatched,
                                            {entries.begin(), entries.end()},
                                            [&](auto) -> std::expected<VriPipeline*, Error>
                                            {
                                                builderCalled = true;
                                                return nullptr;
                                            }),
                     kRejected);
    require(!builderCalled, "A mismatched shader target reached the device pipeline builder");
    const auto                 frame = must(Frame::create(*device));
    const std::array<float, 4> clear {0.1f, 0.2f, 0.7f, 1};
    auto  target = must(Texture::create(*device, support::clearedTexture({129, 73}, clear, VriFormat_BGRA8_UNORM)));
    Image baseline;
    auto  render = [&](Triangle& triangle)
    {
        auto* commands = must(frame->begin());
        target.transition(
            commands,
            {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        must(triangle.draw(commands, target, clear.data()));
        must(frame->submitAndWait());
        return must(readback(*device, target));
    };
    {
        const auto triangle = must(Triangle::create(*device,
                                                    VriFormat_BGRA8_UNORM,
                                                    source,
                                                    root / "source",
                                                    {includes.begin(), includes.end()}));
        baseline            = render(*triangle);
    }
    fs::remove(source);
    fs::remove(root / "source/color.slangh");
    // The ordinary source path resolves its cooked sibling, without includes or an existing watch directory.
    const auto shipped =
        must(Triangle::create(*device, VriFormat_BGRA8_UNORM, source, root / "missing-watch-root", {}));
    require(!shipped->pipeline->poll(), "Cooked program unexpectedly created a watcher");
    require(must(compare(baseline, render(*shipped))).mse == 0, "Source and source-free cooked GPU pixels differ");
    const auto handle     = shipped->pipeline->handle();
    const auto generation = shipped->pipeline->generation();
    auto       corrupt    = deviceCook;
    corrupt.back() ^= 1;
    must(writeFileAtomically(cooked, std::as_bytes(std::span(corrupt))));
    require(!shipped->pipeline->reload() && shipped->pipeline->handle() == handle &&
                shipped->pipeline->generation() == generation,
            "Corrupt cooked replacement destroyed last good pipeline");
    require(shipped->pipeline->diagnostics().find("checksum") != std::string::npos &&
                must(compare(baseline, render(*shipped))).mse == 0,
            "Cooked failure lost diagnostics or changed image");
    must(writeFileAtomically(cooked, std::as_bytes(std::span(deviceCook))));
    require(shipped->pipeline->reload() && shipped->pipeline->generation() == generation + 1 &&
                must(compare(baseline, render(*shipped))).mse == 0,
            "Cooked replacement recovery failed");
    const auto explicitCooked = must(Triangle::create(*device, VriFormat_BGRA8_UNORM, cooked, {}, {}));
    require(must(compare(baseline, render(*explicitCooked))).mse == 0, "Explicit cooked file changed pixels");
    std::puts("Shader cooking passed: source-free GPU parity, metadata validation and replacement failure/recovery");
    return 0;
}
