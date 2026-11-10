#include "../examples/common/triangle.hpp"

#include <vultra/assets/asset_source.hpp>
#include <vultra/assets/scene_render_data.hpp>
#include <vultra/assets/source_file.hpp>
#include <vultra/drivers/rhi/image_readback.hpp>
#include <vultra/platform/os/file.hpp>
#include <vultra/servers/rendering/builtin/openpbr_luts.hpp>
#include <vultra/servers/rendering/builtin/visibility_raster_pipeline.hpp>
#include <vultra/servers/rendering/builtin/visibility_resolve_pipeline.hpp>
#include <vultra/servers/rendering/builtin/visibility_scene_bindings.hpp>
#include <vultra/servers/rendering/graph/render_graph.hpp>

#include <nlohmann/json.hpp>
#include <xxhash.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <memory>
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

    VriResult VRI_CALL rejectPipelineRetirement(VriDevice*)
    {
        return VriResult_Failure;
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
        require(!vultra::ShaderProgram::loadExpected(std::as_bytes(std::span(bytes)), file),
                "Expected shader loader accepted malformed metadata");
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
    const auto source = root / "source/triangle.slang";
    const auto cooked = root / "source/triangle.vshaderc";
    fs::copy_file("examples/offline/shaders/triangle.slang", source);
    fs::copy_file("examples/offline/shaders/color.slangh", root / "source/color.slangh");
    const std::array includes {fs::path("builtin/shaders"), fs::path("examples/common")};
    const std::array entries {ShaderEntry {"vertexMain", VriShaderStage_Vertex},
                              ShaderEntry {"fragmentMain", VriShaderStage_Fragment}};
    const auto       program = ShaderProgram::compile(source, {}, includes);
    require(program.shaders.size() == 2 && !program.rayQuery, "Shader entry discovery failed");
    const auto expectedDescriptors = program.descriptorsExpected(entries);
    require(expectedDescriptors && expectedDescriptors->size() == entries.size(),
            "Expected shader descriptors rejected valid entries");
    require(!program.descriptorsExpected({}), "Expected shader descriptors accepted an empty entry list");
    const std::array missingEntries {ShaderEntry {"missing", VriShaderStage_Fragment}};
    require(!program.descriptorsExpected(missingEntries), "Expected shader descriptors accepted a missing entry");
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
    const auto expectedMemory = ShaderProgram::loadExpected(std::as_bytes(std::span(original)), "embedded/triangle.vshaderc");
    require(expectedMemory && expectedMemory->shaders.size() == fromMemory.shaders.size() &&
                expectedMemory->shaders[0].words == fromMemory.shaders[0].words &&
                expectedMemory->compileKey == fromMemory.compileKey &&
                expectedMemory->resourceBindings().size() == fromMemory.resourceBindings().size(),
            "Expected shader loader changed the cooked program");
    const std::array visibilityIncludes {fs::path("builtin/shaders"), fs::path("external")};
    const auto visibility = ShaderProgram::compile("builtin/shaders/passes/visibility_lighting.slang", {}, visibilityIncludes);
    const auto visibilityFile = root / "visibility_lighting.vshaderc";
    visibility.save(visibilityFile);
    const auto visibilityBytes = readBytes(visibilityFile);
    const auto visibilityExpected = ShaderProgram::loadExpected(std::as_bytes(std::span(visibilityBytes)), visibilityFile);
    require(visibilityExpected && visibilityExpected->shaders.size() == visibility.shaders.size() &&
                visibilityExpected->parameters.fields.size() == visibility.parameters.fields.size() &&
                visibilityExpected->shaders[0].words == visibility.shaders[0].words,
            "Expected shader loader rejected visibility resolve");
    require(fromMemory.shaders.size() == restored.shaders.size() &&
                fromMemory.shaders[0].words == restored.shaders[0].words &&
                fromMemory.shaders[1].words == restored.shaders[1].words &&
                fromMemory.compileKey == restored.compileKey,
            "Embedded shader archive differs from filesystem loading");
    auto damaged = original;
    damaged.back() ^= 1;
    require(!ShaderProgram::loadExpected(std::as_bytes(std::span(damaged)), "embedded/damaged.vshaderc") &&
                !ShaderProgram::loadExpected(std::as_bytes(std::span(original)).first(15), "embedded/truncated.vshaderc"),
            "Expected shader loader accepted a corrupt archive");
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
    {
        auto expectedLuts = createOpenPbrLutsExpected(device);
        require(expectedLuts && (*expectedLuts)->desc.format == VriFormat_RGBA32_SFLOAT &&
                    (*expectedLuts)->desc.width == 32 && (*expectedLuts)->desc.height == 2210,
                "Expected OpenPBR LUT default storage changed");
        const auto core = device.core;
        device.core.MapBuffer = [](VriBuffer*, uint64_t, uint64_t) -> void* { return nullptr; };
        const auto failedLuts = createOpenPbrLutsExpected(device);
        device.core = core;
        require(!failedLuts && failedLuts.error().find("Upload OpenPBR LUT: Map") != std::string::npos,
                "Expected OpenPBR LUT upload did not report a staging map failure");
        auto recoveredLuts = createOpenPbrLutsExpected(device);
        require(recoveredLuts && readback(device, **expectedLuts).rgba == readback(device, **recoveredLuts).rgba,
                "Expected OpenPBR LUT upload did not recover after a staging map failure");
    }
    if (auto bindless = Device::create(false, nullptr, VriFeature_Bindless); bindless)
    {
        auto resolve = VisibilityResolvePipeline::create(**bindless, *visibilityExpected, 71, sizeof(SceneVertex));
        if (!resolve)
            std::fprintf(stderr, "Visibility resolve pipeline: %s\n", resolve.error().c_str());
        require(resolve && (*resolve)->layout() && (*resolve)->handle(), "Create visibility resolve pipeline");
        require(!VisibilityResolvePipeline::create(**bindless, *visibilityExpected, 0, sizeof(SceneVertex)),
                "Visibility resolve accepted no materials");
        {
            SceneData scene;
            scene.vertices = {{{-1, -1, 0}, {0, 0, 1}, {0, 0}},
                              {{1, -1, 0}, {0, 0, 1}, {1, 0}},
                              {{0, 1, 0}, {0, 0, 1}, {.5f, 1}}};
            scene.indices  = {0, 1, 2};
            scene.primitives.push_back({0, 3, 0});
            scene.materials.emplace_back();
            PreparedTextures prepared;
            TextureData pixel;
            pixel.format = TextureFormat::eRgba8Unorm;
            pixel.levels.push_back({{1, 1}, {std::byte {255}, std::byte {255}, std::byte {255}, std::byte {255}}});
            prepared.images.push_back(std::move(pixel));
            prepared.materials.emplace_back();
            GpuScene gpuScene(**bindless, scene, prepared);
            const std::array visible {uint32_t(0)};
            auto draws = visibilityRasterDraws(gpuScene, visible);
            require(draws && draws->size() == 1 && (*draws)[0].firstIndex == 0 && (*draws)[0].indexCount == 3 &&
                        (*draws)[0].material.instance.x == 0 && !(*draws)[0].alphaMasked,
                    "Visibility scene draw conversion failed");
            gpuScene.materials[0].alphaCutoff = .5f;
            auto maskedDraws = visibilityRasterDraws(gpuScene, visible);
            require(maskedDraws && (*maskedDraws)[0].alphaMasked && (*maskedDraws)[0].material.flags.x == .5f,
                    "Visibility scene draw conversion lost alpha masking");
            const std::array invalidVisible {uint32_t(1)};
            require(!visibilityRasterDraws(gpuScene, invalidVisible), "Visibility scene accepted an invalid primitive");
            auto oneMaterial =
                VisibilityResolvePipeline::create(**bindless, *visibilityExpected, 1, gpuScene.vertices->desc.size);
            require(bool(oneMaterial), "Create single-material visibility resolve pipeline");
            VriSamplerDesc samplerDesc {};
            samplerDesc.minFilter = samplerDesc.magFilter = VriFilter_Linear;
            samplerDesc.mipmapMode                        = VriMipmapMode_Linear;
            samplerDesc.maxLod                            = 32;
            VriDescriptor* sampler = nullptr;
            require((*bindless)->core.CreateSampler((*bindless)->handle, &samplerDesc, &sampler) == VriResult_Success && sampler,
                    "Create visibility test sampler");
            const std::array samplers {sampler};
            auto bindings = VisibilitySceneBindings::create(**bindless, gpuScene, **oneMaterial, samplers);
            if (!bindings) std::fprintf(stderr, "Visibility bindings: %s\n", bindings.error().c_str());
            require(bool(bindings), "Create visibility scene bindings");
            require(!VisibilitySceneBindings::create(**bindless, gpuScene, **resolve, samplers),
                    "Visibility bindings accepted a mismatched material layout");
            require(!(*bindings)->updateFrame({}, {}), "Visibility bindings accepted null frame descriptors");
            const auto core = (*bindless)->core;
            (*bindless)->core.MapBuffer = [](VriBuffer*, uint64_t, uint64_t) -> void* { return nullptr; };
            const auto failedUpdate = (*bindings)->updateMaterials();
            (*bindless)->core = core;
            require(!failedUpdate && (*bindings)->updateMaterials(), "Visibility material update did not handle map failure");
            bindings->reset();
            (*bindless)->core.DestroyDescriptor(sampler);
        }
        std::puts("Visibility resolve bindless pipeline creation passed");
    }
    else
        std::puts("Visibility resolve bindless pipeline creation skipped: bindless device unavailable");
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
    const auto wgslExpected = ShaderProgram::loadExpected(std::as_bytes(std::span(wgslBytes)), "embedded/triangle-wgsl.vshaderc");
    require(wgslExpected && wgslExpected->shaders[0].wgsl == wgslRestored.shaders[0].wgsl,
            "Expected shader loader changed WGSL");
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
        bool expectedBuilderCalled = false;
        enum class BuildResult
        {
            eNative,
            eRejected,
            eNull,
            eAlias
        };
        auto       buildResult   = BuildResult::eNative;
        const auto buildExpected = [&](std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, std::string>
        {
            expectedBuilderCalled = true;
            if (buildResult == BuildResult::eRejected)
            {
                return std::unexpected("injected pipeline builder rejection");
            }
            if (buildResult == BuildResult::eNull)
            {
                return nullptr;
            }
            if (buildResult == BuildResult::eAlias)
            {
                return triangle.pipeline->handle();
            }
            VriColorAttachmentDesc color {};
            color.format = VriFormat_BGRA8_UNORM;
            color.colorWriteMask = VriColorWrite_RGBA;
            VriGraphicsPipelineDesc desc {};
            desc.pipelineCache = device.pipelineCache;
            desc.pipelineLayout = triangle.layout;
            desc.shaders = shaders.data();
            desc.shaderNum = uint32_t(shaders.size());
            desc.inputAssembly.topology = VriPrimitiveTopology_TriangleList;
            desc.rasterization.cullMode = VriCullMode_None;
            desc.rasterization.lineWidth = 1;
            desc.multisample.sampleNum = 1;
            desc.outputMerger.colors = &color;
            desc.outputMerger.colorNum = 1;
            VriPipeline* pipeline = nullptr;
            const auto result = device.core.CreateGraphicsPipeline(device.handle, &desc, &pipeline);
            if (result != VriResult_Success)
                return std::unexpected("Create triangle pipeline failed: " + std::to_string(result));
            return pipeline;
        };
        auto incompatible = ShaderPipeline::create(device, wgslRestored, {entries.begin(), entries.end()}, buildExpected);
        require(!incompatible && !expectedBuilderCalled, "Expected pipeline accepted an incompatible target");
        auto missing = ShaderPipeline::create(device, program, {missingEntries.begin(), missingEntries.end()}, buildExpected);
        require(!missing && !expectedBuilderCalled, "Expected pipeline accepted a missing entry");
        auto failed = ShaderPipeline::create(device,
                                             program,
                                             {entries.begin(), entries.end()},
                                             [](auto) -> std::expected<VriPipeline*, std::string>
                                             {
                                                 return std::unexpected("builder failure");
                                             });
        require(!failed && failed.error() == "builder failure", "Expected pipeline lost builder error");
        auto empty = ShaderPipeline::create(device,
                                            program,
                                            {entries.begin(), entries.end()},
                                            [](auto) -> std::expected<VriPipeline*, std::string>
                                            {
                                                return nullptr;
                                            });
        require(!empty, "Expected pipeline accepted a null handle");
        std::weak_ptr<int> builderCapture;
        const auto         transientBuilder = [&]
        {
            const auto token = std::make_shared<int>(0);
            builderCapture   = token;
            return ShaderPipeline::ExpectedBuilder(
                [&, token](std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, std::string>
                {
                    (void)token;
                    return buildExpected(shaders);
                });
        };
        auto created = ShaderPipeline::create(device, program, {entries.begin(), entries.end()}, transientBuilder());
        require(builderCapture.expired(), "Immutable pipeline retained a transient builder capture");
        require(created && expectedBuilderCalled && (*created)->generation() == 1 && (*created)->handle(),
                "Expected pipeline creation failed");
        triangle.pipeline = std::move(*created);
        require(compare(baseline, render(triangle)).mse == 0, "Expected pipeline changed GPU pixels");

        const auto retained = [&]
        {
            const auto handle     = triangle.pipeline->handle();
            const auto generation = triangle.pipeline->generation();
            const auto identity   = triangle.pipeline->identity();
            return [&, handle, generation, identity]
            {
                const auto& current = triangle.pipeline->identity();
                require(triangle.pipeline->handle() == handle && triangle.pipeline->generation() == generation &&
                            current.file == identity.file && current.compileKey == identity.compileKey &&
                            current.bytecodeHash == identity.bytecodeHash &&
                            std::ranges::equal(current.dependencies,
                                               identity.dependencies,
                                               [](const auto& a, const auto& b)
                                               {
                                                   return a.path == b.path && a.hash == b.hash;
                                               }),
                        "Rejected pipeline replacement changed the accepted identity");
                require(compare(baseline, render(triangle)).mse == 0, "Rejected replacement changed accepted pixels");
            };
        };
        std::function<void()> unchanged = retained();
        require(!triangle.pipeline->reloadExpected(), "In-memory pipeline unexpectedly reloaded a file");
        unchanged();

        ShaderCompileOptions pipelineOptions;
        pipelineOptions.entries.assign(entries.begin(), entries.end());
        pipelineOptions.includeDirectories.assign(includes.begin(), includes.end());
        pipelineOptions.defines = {{"PIPELINE_EXPECTED_TEST", "1"}};
        auto sourced            = ShaderPipeline::create(device,
                                              source,
                                              pipelineOptions,
                                              buildExpected,
                                              root / "source",
                                              root / "pipeline-cache");
        require(bool(sourced), sourced ? "" : sourced.error().c_str());
        triangle.pipeline = std::move(*sourced);
        require(compare(baseline, render(triangle)).mse == 0 && !triangle.pipeline->identity().compileKey.empty(),
                "Expected source pipeline changed pixels or lost cache identity");
        require(!ShaderPipeline::create(device,
                                        source,
                                        pipelineOptions,
                                        buildExpected,
                                        root / "absent-watch",
                                        root / "pipeline-cache"),
                "Expected pipeline accepted an absent source watch root");
        require(!ShaderPipeline::create(device, root / "bad.extension", pipelineOptions, buildExpected),
                "Expected pipeline accepted an unsupported file extension");
        require(
            !ShaderPipeline::create(device, fs::path(std::string("bad\0source", 10)), pipelineOptions, buildExpected),
            "Expected pipeline accepted a NUL path");
        require(!ShaderPipeline::create(device, source, pipelineOptions, ShaderPipeline::ExpectedBuilder {}),
                "Expected file pipeline accepted an empty builder");
        unchanged = retained();
        for (const auto result : {BuildResult::eRejected, BuildResult::eNull, BuildResult::eAlias})
        {
            buildResult         = result;
            const auto rejected = triangle.pipeline->reloadExpected();
            require(!rejected && !rejected.error().empty(), "Expected reload accepted a rejected/null/aliased build");
            if (result == BuildResult::eRejected)
            {
                require(rejected.error().find("injected pipeline builder rejection") != std::string::npos,
                        "Expected reload lost builder diagnostics");
            }
            unchanged();
        }
        buildResult                = BuildResult::eNative;
        const auto idle            = device.core.DeviceWaitIdle;
        device.core.DeviceWaitIdle = rejectPipelineRetirement;
        const auto retired         = triangle.pipeline->reloadExpected();
        device.core.DeviceWaitIdle = idle;
        require(!retired && retired.error().find("Retire previous") != std::string::npos,
                "Expected reload lost device-retirement rejection");
        unchanged();
        const auto write = [](const fs::path& file, std::string_view text)
        {
            const auto result = writeFileAtomicallyExpected(file, std::as_bytes(std::span(text)));
            require(bool(result), result ? "" : result.error().c_str());
        };
        const auto colorFile = root / "source/color.slangh";
        write(colorFile, "invalid source @@@");
        require(!triangle.pipeline->reloadExpected() && !triangle.pipeline->diagnostics().empty(),
                "Expected source reload accepted a compiler failure");
        unchanged();
        const auto external = root / "external-dep";
        fs::create_directory(external);
        const auto implementation = external / "implementation.slangh";
        write(implementation, "float3 experimentColor(float3 c) { return c * 0.25; }\n");
        write(colorFile, "#include \"../external-dep/implementation.slangh\"\n");
        buildResult = BuildResult::eRejected;
        require(!triangle.pipeline->reloadExpected(), "External dependency unexpectedly bypassed builder rejection");
        unchanged();
        // Drain notifications from the source edit before changing only the external dependency.
        const auto quiet = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (std::chrono::steady_clock::now() < quiet)
        {
            const auto changed = triangle.pipeline->pollExpected();
            require(!changed || !*changed, "Rejected builder unexpectedly published a watched program");
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        buildResult = BuildResult::eNative;
        write(implementation, "float3 experimentColor(float3 c) { return c * 0.5; }\n");
        bool       published = false;
        const auto deadline  = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!published && std::chrono::steady_clock::now() < deadline)
        {
            const auto changed = triangle.pipeline->pollExpected();
            require(bool(changed), changed ? "" : changed.error().c_str());
            published = *changed;
            if (!published)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        }
        require(published, "Expected watcher failed to publish an included-source edit");
        const auto changedImage = render(triangle);
        require(compare(baseline, changedImage).mse > 0, "Expected source edit did not change GPU pixels");
        {
            Triangle reference(device,
                               VriFormat_BGRA8_UNORM,
                               source,
                               root / "source",
                               {includes.begin(), includes.end()});
            require(compare(changedImage, render(reference)).mse == 0,
                    "Expected hot reload differs from a fresh source pipeline");
        }
        write(colorFile, "float3 experimentColor(float3 c) { return c; }\n");
        fs::remove_all(external);
        require(bool(triangle.pipeline->reloadExpected()) && compare(baseline, render(triangle)).mse == 0,
                "Expected source reload failed to recover reference pixels");

        auto directory = AssetSource::create(root / "source");
        require(bool(directory), directory ? "" : directory.error().c_str());
        const std::array packageFiles {VpkFile {"triangle.vshaderc", cooked}};
        const auto       packageFile = root / "pipeline.vpk";
        VpkArchive::pack(packageFiles, packageFile);
        auto diskArchive = VpkArchive::open(packageFile);
        require(bool(diskArchive), diskArchive ? "" : diskArchive.error().c_str());
        auto disk = AssetSource::create(std::move(*diskArchive));
        require(bool(disk), disk ? "" : disk.error().c_str());
        auto packageBytes = readSourceFileExpected(packageFile);
        require(bool(packageBytes), packageBytes ? "" : packageBytes.error().c_str());
        auto memoryArchive = VpkArchive::open(root / "virtual-pipeline.vpk", *packageBytes);
        require(bool(memoryArchive), memoryArchive ? "" : memoryArchive.error().c_str());
        auto memory = AssetSource::create(std::move(*memoryArchive));
        require(bool(memory), memory ? "" : memory.error().c_str());
        for (const auto* provider : {directory->get(), disk->get(), memory->get()})
        {
            auto packaged = ShaderPipeline::create(device,
                                                   "triangle.slang",
                                                   pipelineOptions,
                                                   buildExpected,
                                                   root / "absent-watch",
                                                   root / "not-a-cache",
                                                   provider);
            require(bool(packaged), packaged ? "" : packaged.error().c_str());
            triangle.pipeline  = std::move(*packaged);
            const auto changed = triangle.pipeline->pollExpected();
            require(changed && !*changed && compare(baseline, render(triangle)).mse == 0,
                    "Expected packaged pipeline created a watcher or changed GPU pixels");
            require(bool(triangle.pipeline->reloadExpected()) && compare(baseline, render(triangle)).mse == 0,
                    "Expected packaged pipeline failed to reload checked source bytes");
        }
        triangle.pipeline.reset(); // Borrowed provider bytes and sources outlive their final GPU use.
        std::puts("Expected pipelines: source/watch/edit, builder/null/alias/idle rejection, identity retention, "
                  "directory/disk/memory VPK GPU parity passed");
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
