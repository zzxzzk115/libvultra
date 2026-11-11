#include "../examples/common/triangle.hpp"

#include <vultra/gpu/image_readback.hpp>
#include <vultra/graph/render_graph.hpp>
#include <vultra/platform/os/file.hpp>

#include <nlohmann/json.hpp>
#include <xxhash.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
    void require(bool value, const char* message)
    {
        if (!value)
        {
            throw std::runtime_error(message);
        }
    }

    template<class Callback>
    void reject(Callback&& callback)
    {
        bool rejected = false;
        try
        {
            callback();
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        require(rejected, "Expected shader input rejection");
    }

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
        auto document = nlohmann::json::from_cbor(original.begin() + 16, original.end());
        callback(document);
        const auto           payload = nlohmann::json::to_cbor(document);
        std::vector<uint8_t> bytes(original.begin(), original.begin() + 16);
        const auto           hash = XXH3_64bits(payload.data(), payload.size());
        for (size_t i = 0; i < sizeof(hash); ++i)
        {
            bytes[8 + i] = uint8_t(hash >> (8 * i));
        }
        bytes.insert(bytes.end(), payload.begin(), payload.end());
        vultra::writeFileAtomically(file, std::as_bytes(std::span(bytes)));
        reject(
            [&]
            {
                vultra::ShaderProgram::load(file);
            });
    }

    void resourceViews(const std::filesystem::path& root)
    {
        using namespace vultra;
        const auto program = ShaderProgram::compileSource(root / "resource_views.slang", R"(
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
)");
        const auto file    = root / "resource_views.vshaderc";
        program.save(file);
        const auto       restored = ShaderProgram::load(file);
        const std::array expected {std::pair {"line", VriTextureViewType_1D},
                                   std::pair {"lines", VriTextureViewType_1DArray},
                                   std::pair {"plane", VriTextureViewType_2D},
                                   std::pair {"planes", VriTextureViewType_2DArray},
                                   std::pair {"volume", VriTextureViewType_3D},
                                   std::pair {"cube", VriTextureViewType_Cube},
                                   std::pair {"cubes", VriTextureViewType_CubeArray}};
        for (const auto* candidate : {&program, &restored})
        {
            const auto bindings = candidate->resourceBindings();
            for (const auto& [name, view] : expected)
            {
                const auto binding = std::ranges::find(bindings, name, &ShaderResourceBinding::name);
                require(binding != bindings.end() && binding->type == VriDescriptorType_Texture &&
                            binding->viewType == view,
                        "Compiler/cooked reflection lost a VRI texture dimension");
            }
        }
    }

    void userAttributes(const std::filesystem::path& root)
    {
        using namespace vultra;
        const auto program = ShaderProgram::compileSource(root / "attributes.slang", R"(
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
)");
        const auto file    = root / "attributes.vshaderc";
        program.save(file);
        const auto restored = ShaderProgram::load(file);
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
            return ShaderProgram::compileSource(root / "modes.slang", source, options);
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
        Device                      device;
        VriPipelineLayout*          layout = nullptr;
        const VriPipelineLayoutDesc layoutDesc {};
        check(device.core.CreatePipelineLayout(device.handle, &layoutDesc, &layout), "Watched reload layout");
        {
            ShaderPipeline pipeline(device,
                                    file,
                                    {{"main", VriShaderStage_Compute}},
                                    [&](std::span<const VriShaderDesc> shaders)
                                    {
                                        VriPipeline*                 result = nullptr;
                                        const VriComputePipelineDesc desc {layout, shaders[0], device.pipelineCache};
                                        check(device.core.CreateComputePipeline(device.handle, &desc, &result),
                                              "Watched reload pipeline");
                                        return result;
                                    });
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
        device.core.DestroyPipelineLayout(layout);
    }
} // namespace

int main()
try
{
    using namespace vultra;
    namespace fs    = std::filesystem;
    const auto root = fs::path("build/.tmp/shader-program") /
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    fs::create_directories(root / "source");
    resourceViews(root);
    userAttributes(root);
    compileModes(root);
    watchedReload(root);
    const auto source = root / "source/triangle.slang";
    const auto cooked = root / "source/triangle.vshaderc";
    fs::copy_file("examples/offline/shaders/triangle.slang", source);
    fs::copy_file("examples/offline/shaders/color.slangh", root / "source/color.slangh");
    const std::array includes {fs::path("builtin/shaders"), fs::path("examples/common")};
    const std::array entries {ShaderEntry {"vertexMain", VriShaderStage_Vertex},
                              ShaderEntry {"fragmentMain", VriShaderStage_Fragment}};
    const auto       program = ShaderProgram::compile(source, {}, includes);
    require(program.shaders.size() == 2 && !program.rayQuery, "Shader entry discovery failed");
    program.save(cooked);
    ShaderCompileOptions cacheOptions;
    cacheOptions.includeDirectories.assign(includes.begin(), includes.end());
    require(!ShaderProgram::cook(source, cooked, cacheOptions), "Raw Slang unchanged cache missed");
    auto defineOptions = cacheOptions;
    defineOptions.defines.push_back({"TEST_CACHE_DEFINE", "1"});
    require(ShaderProgram::cook(source, cooked, defineOptions) && !ShaderProgram::cook(source, cooked, defineOptions),
            "Macro options did not invalidate/cache raw Slang");
    program.save(cooked);
    const auto original   = readBytes(cooked);
    const auto restored   = ShaderProgram::load(cooked);
    const auto fromMemory = ShaderProgram::load(std::as_bytes(std::span(original)), "embedded/triangle.vshaderc");
    require(fromMemory.shaders.size() == restored.shaders.size() &&
                fromMemory.shaders[0].words == restored.shaders[0].words &&
                fromMemory.shaders[1].words == restored.shaders[1].words &&
                fromMemory.compileKey == restored.compileKey,
            "Embedded shader archive differs from filesystem loading");
    auto damaged = original;
    damaged.back() ^= 1;
    reject(
        [&]
        {
            ShaderProgram::load(std::as_bytes(std::span(damaged)), "embedded/damaged.vshaderc");
        });
    reject(
        [&]
        {
            ShaderProgram::load(std::as_bytes(std::span(original)).first(15), "embedded/truncated.vshaderc");
        });
    require(restored.shaders.size() == program.shaders.size() &&
                restored.shaders[0].words == program.shaders[0].words &&
                restored.shaders[1].words == program.shaders[1].words,
            "Cooked bytecode changed during roundtrip");
    const auto views = restored.descriptors(entries);
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
    const auto             rayProgram = ShaderProgram::compileSource(root / "ray.slang", raySource);
    require(rayProgram.shaders.size() == rayEntries.size(), "Ray tracing entry discovery failed");
    const auto rayCooked = root / "ray.vshaderc";
    rayProgram.save(rayCooked);
    const auto           rayRestored = ShaderProgram::load(rayCooked);
    const auto           rayViews    = rayRestored.descriptors(rayEntries);
    ShaderCompileOptions rayOptions;
    rayOptions.entries.assign(rayEntries.begin(), rayEntries.end());
    const auto rayRequested = ShaderProgram::compileSource(root / "ray.slang", raySource, rayOptions);
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
                      document["entries"][0]["stage"] = uint32_t(VriShaderStage_RayGen | VriShaderStage_Miss);
                  });
    const std::array wrongStage {ShaderEntry {"vertexMain", VriShaderStage_Fragment}};
    reject(
        [&]
        {
            ShaderProgram::compile(source, wrongStage, includes);
        });
    reject(
        [&]
        {
            restored.descriptors(wrongStage);
        });
    const std::array missing {ShaderEntry {"missing", VriShaderStage_Vertex}};
    reject(
        [&]
        {
            restored.descriptors(missing);
        });
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
                      document["target"] = "dxil";
                  });
    alterDocument(cooked,
                  original,
                  [](auto& document)
                  {
                      document["entries"][0]["stage"] = uint64_t(0x100000001);
                  });
    alterDocument(cooked,
                  original,
                  [](auto& document)
                  {
                      document["entries"][1]["name"] = "vertexMain";
                  });
    alterDocument(cooked,
                  original,
                  [](auto& document)
                  {
                      document["entries"][0]["code"] = nlohmann::json::binary({1, 2, 3});
                  });
    alterDocument(cooked,
                  original,
                  [](auto& document)
                  {
                      document["parameters"]["descriptor_type"] = 99;
                  });
    alterDocument(cooked,
                  original,
                  [](auto& document)
                  {
                      document["parameters"]["view_type"] = 99;
                  });
    writeFileAtomically(cooked, std::as_bytes(std::span(original)));
    auto invalid                = program;
    invalid.shaders[0].words[0] = 0;
    reject(
        [&]
        {
            invalid.save(cooked);
        });
    require(readBytes(cooked) == original, "Rejected shader save overwrote last good artifact");

    Device               device;
    ShaderCompileOptions wgslOptions;
    wgslOptions.profile = "wgsl";
    wgslOptions.includeDirectories.assign(includes.begin(), includes.end());
    const auto wgsl = ShaderProgram::compile(source, wgslOptions);
    require(wgsl.profile == "wgsl" && wgsl.compileKey != program.compileKey && wgsl.shaders.size() == 2,
            "WGSL target shared the SPIR-V compiler identity or lost shader entries");
    for (const auto& shader : wgsl.shaders)
    {
        require(shader.words.empty() && !shader.wgsl.empty(), "WGSL program retained the wrong target payload");
    }
    const auto wgslFile = root / "triangle-wgsl.vshaderc";
    wgsl.save(wgslFile);
    const auto wgslBytes = readBytes(wgslFile);
    const auto wgslRestored =
        ShaderProgram::load(std::as_bytes(std::span(wgslBytes)), "embedded/triangle-wgsl.vshaderc");
    require(wgslRestored.profile == "wgsl" && wgslRestored.shaders[0].wgsl == wgsl.shaders[0].wgsl &&
                wgslRestored.shaders[1].wgsl == wgsl.shaders[1].wgsl,
            "Cooked WGSL changed during embedded archive loading");
    const auto wgslDescriptors = wgslRestored.descriptors(entries);
    require(std::string_view(static_cast<const char*>(wgslDescriptors[0].bytecode)) == wgsl.shaders[0].wgsl &&
                wgslDescriptors[0].bytecodeSize == wgsl.shaders[0].wgsl.size() + 1,
            "VRI did not receive owned, NUL-terminated WGSL");
    alterDocument(wgslFile,
                  wgslBytes,
                  [](auto& document)
                  {
                      document["entries"][0]["code"].get_binary().push_back(0);
                  });
    auto invalidWgsl             = wgsl;
    invalidWgsl.shaders[0].words = program.shaders[0].words;
    reject(
        [&]
        {
            invalidWgsl.save(wgslFile);
        });
    wgslOptions.rayQuery = true;
    reject(
        [&]
        {
            ShaderProgram::compile(source, wgslOptions);
        });
    bool builderCalled = false;
    reject(
        [&]
        {
            ShaderPipeline incompatible(device,
                                        wgslRestored,
                                        {entries.begin(), entries.end()},
                                        [&](auto) -> VriPipeline*
                                        {
                                            builderCalled = true;
                                            return nullptr;
                                        });
        });
    require(!builderCalled, "A mismatched shader target reached the device pipeline builder");
    Frame       frame(device);
    Texture     target(device, colorTexture({129, 73}, VriFormat_BGRA8_UNORM));
    Image       baseline;
    const float clear[4] {0.1f, 0.2f, 0.7f, 1};
    auto        render = [&](Triangle& triangle)
    {
        auto* commands = frame.begin();
        target.transition(
            commands,
            {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        triangle.draw(commands, target, clear);
        frame.submitAndWait();
        return readback(device, target);
    };
    {
        Triangle triangle(device, VriFormat_BGRA8_UNORM, source, root / "source", {includes.begin(), includes.end()});
        baseline = render(triangle);
    }
    fs::remove(source);
    fs::remove(root / "source/color.slangh");
    // The ordinary source path resolves its cooked sibling, without includes or an existing watch directory.
    Triangle shipped(device, VriFormat_BGRA8_UNORM, source, root / "missing-watch-root", {});
    require(!shipped.pipeline->poll(), "Cooked program unexpectedly created a watcher");
    require(compare(baseline, render(shipped)).mse == 0, "Source and source-free cooked GPU pixels differ");
    const auto handle     = shipped.pipeline->handle();
    const auto generation = shipped.pipeline->generation();
    auto       corrupt    = original;
    corrupt.back() ^= 1;
    writeFileAtomically(cooked, std::as_bytes(std::span(corrupt)));
    require(!shipped.pipeline->reload() && shipped.pipeline->handle() == handle &&
                shipped.pipeline->generation() == generation,
            "Corrupt cooked replacement destroyed last good pipeline");
    require(shipped.pipeline->diagnostics().find("checksum") != std::string::npos &&
                compare(baseline, render(shipped)).mse == 0,
            "Cooked failure lost diagnostics or changed image");
    writeFileAtomically(cooked, std::as_bytes(std::span(original)));
    require(shipped.pipeline->reload() && shipped.pipeline->generation() == generation + 1 &&
                compare(baseline, render(shipped)).mse == 0,
            "Cooked replacement recovery failed");
    Triangle explicitCooked(device, VriFormat_BGRA8_UNORM, cooked, {}, {});
    require(compare(baseline, render(explicitCooked)).mse == 0, "Explicit cooked file changed pixels");
    std::puts("Shader cooking passed: source-free GPU parity, metadata validation and replacement failure/recovery");
    return 0;
}
catch (const std::exception& error)
{
    std::fprintf(stderr, "Shader program test: %s\n", error.what());
    return 1;
}
