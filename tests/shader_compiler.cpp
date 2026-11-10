#include <vultra/core/base/stable_id.hpp>
#include <vultra/drivers/rhi/resources.hpp>
#include <vultra/drivers/rhi/shader_compiler.hpp>
#include <vultra/drivers/rhi/shader_pipeline.hpp>
#include <vultra/platform/os/file.hpp>
#include <vultra/servers/rendering/shader_material.hpp>

#include <nlohmann/json.hpp>
#include <xxhash.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace
{
    void require(bool value, const char* message)
    {
        if (!value)
        {
            throw std::runtime_error(message);
        }
    }

    void writeText(const std::filesystem::path& file, std::string_view text)
    {
        const auto written =
            vultra::writeFileAtomicallyExpected(file, std::as_bytes(std::span(text.data(), text.size())));
        require(bool(written), written ? "" : written.error().c_str());
    }

    std::vector<uint8_t> readBytes(const std::filesystem::path& file)
    {
        std::ifstream input(file, std::ios::binary);
        require(bool(input), "Open compiler test artifact");
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    template<class T>
    void reject(const std::expected<T, std::string>& result)
    {
        require(!result && !result.error().empty(), "Expected boundary rejection with diagnostics");
    }

    vultra::ShaderProgram compile(vultra::ShaderCompiler&             compiler,
                                  const std::filesystem::path&        file,
                                  const vultra::ShaderCompileOptions& options)
    {
        auto program = compiler.compileExpected(file, options);
        require(bool(program), program ? "" : program.error().c_str());
        return std::move(*program);
    }

    class ComputeProbe
    {
    public:
        explicit ComputeProbe(const std::filesystem::path& root) :
            m_File(root / "probe.vshaderc"),
            m_Frame(m_Device),
            m_Output(m_Device,
                     {4, 4, VriBufferUsage_StorageBuffer | VriBufferUsage_TransferSrc, VriMemoryLocation_Device}),
            m_Readback(m_Device, {4, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback})
        {
            try
            {
                const VriDescriptorRangeDesc range {0, 1, VriDescriptorType_StorageBuffer, VriShaderStage_Compute};
                const VriDescriptorSetDesc   set {0, &range, 1};
                const VriPipelineLayoutDesc  layout {&set, 1, nullptr, 0, VriShaderStage_Compute};
                vultra::check(m_Device.core.CreatePipelineLayout(m_Device.handle, &layout, &m_Layout),
                              "Create probe layout");
                VriDescriptorPoolDesc pool {};
                pool.descriptorSetMaxNum = 1;
                pool.storageBufferMaxNum = 1;
                vultra::check(m_Device.core.CreateDescriptorPool(m_Device.handle, &pool, &m_Pool), "Create probe pool");
                vultra::check(m_Device.core.AllocateDescriptorSets(m_Pool, m_Layout, 0, &m_Set, 1),
                              "Allocate probe set");
                const VriBufferViewDesc view {m_Output.handle,
                                              VriDescriptorType_StorageBuffer,
                                              VriFormat_Unknown,
                                              0,
                                              4};
                vultra::check(m_Device.core.CreateBufferView(m_Device.handle, &view, &m_View), "Create probe view");
                const VriDescriptor*               descriptor = m_View;
                const VriDescriptorRangeUpdateDesc update {&descriptor, 1, 0};
                m_Device.core.UpdateDescriptorRanges(m_Set, 0, 1, &update);
            }
            catch (...)
            {
                release();
                throw;
            }
        }

        ~ComputeProbe()
        {
            release();
        }

        uint32_t run(const vultra::ShaderProgram& program, bool useDriverCache = true)
        {
            const auto saved = program.saveExpected(m_File);
            require(bool(saved), saved ? "" : saved.error().c_str());
            const auto loaded = vultra::ShaderProgram::loadExpected(m_File);
            require(bool(loaded), loaded ? "" : loaded.error().c_str());
            auto pipeline = vultra::ShaderPipeline::create(
                m_Device,
                *loaded,
                std::vector<vultra::ShaderEntry> {{"main", VriShaderStage_Compute}},
                [&](std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, std::string>
                {
                    const VriComputePipelineDesc desc {m_Layout,
                                                       shaders.front(),
                                                       useDriverCache ? m_Device.pipelineCache : nullptr};
                    VriPipeline*                 result = nullptr;
                    const auto status = m_Device.core.CreateComputePipeline(m_Device.handle, &desc, &result);
                    if (status != VriResult_Success)
                    {
                        return std::unexpected("Create probe pipeline: " + std::to_string(uint32_t(status)));
                    }
                    return result;
                });
            require(bool(pipeline), pipeline ? "" : pipeline.error().c_str());
            auto* commands = m_Frame.begin();
            m_Output.transition(commands, {VriAccess_ShaderResourceStorageWrite, VriPipelineStage_ComputeShader});
            m_Device.core.CmdSetPipelineLayout(commands, m_Layout);
            m_Device.core.CmdSetPipeline(commands, (*pipeline)->handle());
            m_Device.core.CmdSetDescriptorSet(commands, 0, m_Set);
            return dispatchAndRead(commands);
        }

        uint32_t run(const vultra::ShaderAsset& asset, const std::string& variant)
        {
            vultra::MaterialInstance instance;
            instance.setVariant(asset, variant);
            vultra::ShaderMaterial material(m_Device, asset, instance);
            const std::array output {vultra::ShaderResourceViews {"result", VriDescriptorType_StorageBuffer, {m_View}}};
            material.prepare("Experiment", {}, output);
            auto* commands = m_Frame.begin();
            m_Output.transition(commands, {VriAccess_ShaderResourceStorageWrite, VriPipelineStage_ComputeShader});
            material.bind(commands, "Experiment");
            return dispatchAndRead(commands);
        }

    private:
        uint32_t dispatchAndRead(VriCommandBuffer* commands)
        {
            const VriDispatchDesc dispatch {1, 1, 1};
            m_Device.core.CmdDispatch(commands, &dispatch);
            m_Output.transition(commands, {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
            m_Readback.transition(commands, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
            const VriBufferCopyDesc copy {0, 0, 4};
            m_Device.core.CmdCopyBuffer(commands, m_Readback.handle, m_Output.handle, &copy);
            m_Frame.submitAndWait();
            auto* mapped = m_Device.core.MapBuffer(m_Readback.handle, 0, 4);
            require(mapped != nullptr, "Map compiler probe");
            uint32_t value = 0;
            std::memcpy(&value, mapped, sizeof(value));
            m_Device.core.UnmapBuffer(m_Readback.handle);
            return value;
        }

        void release()
        {
            m_Device.waitIdleForDestruction();
            if (m_Pool)
            {
                m_Device.core.DestroyDescriptorPool(m_Pool);
            }
            if (m_View)
            {
                m_Device.core.DestroyDescriptor(m_View);
            }
            if (m_Layout)
            {
                m_Device.core.DestroyPipelineLayout(m_Layout);
            }
        }

        vultra::Device        m_Device;
        std::filesystem::path m_File;
        vultra::Frame         m_Frame;
        vultra::Buffer        m_Output;
        vultra::Buffer        m_Readback;
        VriPipelineLayout*    m_Layout = nullptr;
        VriDescriptorPool*    m_Pool   = nullptr;
        VriDescriptorSet*     m_Set    = nullptr;
        VriDescriptor*        m_View   = nullptr;
    };
} // namespace

int main()
try
{
    using namespace vultra;
    namespace fs    = std::filesystem;
    const auto root = fs::path("build/.tmp/shader-compiler") / StableId::generate().toString();
    fs::create_directories(root / "early");
    fs::create_directories(root / "late");
    const auto                 source = root / "main.slang";
    constexpr std::string_view text   = R"(
#include "value.slangh"
extern static const uint choice;
#ifndef FEATURE
#define FEATURE 0
#endif
[[vk::binding(0, 0)]] RWStructuredBuffer<uint> result;
[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 index : SV_DispatchThreadID)
{
    result[0] = choice * factor + FEATURE;
}
)";
    writeText(source, text);
    const auto include = root / "late/value.slangh";
    writeText(include, "static const uint factor = 3;\n");
    ShaderCompileOptions options;
    options.entries            = {{"main", VriShaderStage_Compute}};
    options.includeDirectories = {root / "early", root / "late"};
    options.linkSources        = {{"choices", "export static const uint choice = 5;"}};
    auto compilerResult        = ShaderCompiler::create(root / "cache");
    require(bool(compilerResult), compilerResult ? "" : compilerResult.error().c_str());
    auto&          compiler = **compilerResult;
    ComputeProbe   probe(root);
    const auto     first = compile(compiler, source, options);
    const auto     cold  = compiler.lastCompileStatistics();
    require(!cold.programCacheHit && !cold.moduleCacheHit && probe.run(first) == 15, "Cold shader output is incorrect");

    auto secondOptions                 = options;
    secondOptions.linkSources[0].value = "export static const uint choice = 9;";
    const auto second                  = compile(compiler, source, secondOptions);
    const auto reused                  = compiler.lastCompileStatistics();
    require(!reused.programCacheHit && reused.moduleCacheHit && probe.run(second) == 27,
            "Reused module leaked a prior variant's link constants");
    require(first.resourceBindings().size() == second.resourceBindings().size() &&
                second.resourceBindings().front().binding == 0 && second.shaders.front().threadGroup[0] == 1,
            "Reused IR lost target resource/entry reflection");

    auto reopenedResult = ShaderCompiler::create(root / "cache");
    require(bool(reopenedResult), reopenedResult ? "" : reopenedResult.error().c_str());
    auto&          reopened = **reopenedResult;
    const auto     warm     = compile(reopened, source, secondOptions);
    const auto     cached = reopened.lastCompileStatistics();
    require(cached.programCacheHit && cached.frontendMilliseconds == 0 && cached.codegenMilliseconds == 0 &&
                warm.shaders.front().words == second.shaders.front().words && probe.run(warm) == 27,
            "Reopened program cache compiled again or changed GPU output");

    auto modeOptions             = secondOptions;
    modeOptions.optimization     = ShaderOptimization::eHigh;
    modeOptions.debugInformation = ShaderDebugInformation::eNone;
    const auto optimized         = compile(compiler, source, modeOptions);
    require(optimized.compileKey != second.compileKey && !compiler.lastCompileStatistics().moduleCacheHit &&
                probe.run(optimized) == 27,
            "Optimization level reused incompatible IR or changed GPU output");
    const auto optimizedCached = compile(reopened, source, modeOptions);
    require(reopened.lastCompileStatistics().programCacheHit &&
                optimizedCached.shaders.front().words == optimized.shaders.front().words,
            "Optimization level did not retain its own program cache");
    modeOptions.debugInformation = ShaderDebugInformation::eStandard;
    const auto debugProgram      = compile(compiler, source, modeOptions);
    require(debugProgram.compileKey != optimized.compileKey && !compiler.lastCompileStatistics().moduleCacheHit &&
                probe.run(debugProgram) == 27,
            "Debug information reused incompatible IR or changed GPU output");
    const auto debugCached = compile(reopened, source, modeOptions);
    require(reopened.lastCompileStatistics().programCacheHit &&
                debugCached.shaders.front().words == debugProgram.shaders.front().words,
            "Debug information did not retain its own program cache");

    constexpr std::string_view nestedSource = R"(
[[vk::binding(0, 0)]] RWStructuredBuffer<uint> result;
struct State { uint value; uint offset; };
uint sampleValue(inout State state, uint mode)
{
    if (mode == 0)
    {
        state.value += state.offset;
        return state.value;
    }
    state.value = (state.value + state.offset) * 3;
    return state.value + mode;
}
uint evaluate(uint count, uint stride)
{
    State state;
    state.value = count * 7;
    state.offset = stride + 1;
    return sampleValue(state, count);
}
[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
    uint count;
    uint stride;
    result.GetDimensions(count, stride);
    result[0] = evaluate(count, stride);
}
)";
    std::array<std::string, 4> modeKeys;
    for (size_t mode = 0; mode < modeKeys.size(); ++mode)
    {
        ShaderCompileOptions nestedOptions;
        nestedOptions.optimization = ShaderOptimization(mode);
        auto nested                = compiler.compileSourceExpected(root / "nested.slang", nestedSource, nestedOptions);
        require(bool(nested), nested ? "" : nested.error().c_str());
        require(probe.run(*nested) == 37 && nested->resourceBindings().size() == 1 &&
                    nested->resourceBindings().front().binding == 0 && nested->shaders.front().threadGroup[0] == 1,
                "Nested structure/early-return compilation changed GPU output or reflected resource ABI");
        modeKeys[mode] = nested->compileKey;
        for (size_t prior = 0; prior < mode; ++prior)
        {
            require(modeKeys[mode] != modeKeys[prior], "Optimization modes reused the same compiler cache key");
        }
    }
    std::puts("SPIRV_OPTIMIZATION modes=4 nested_struct_gpu=37 binding_and_entry=retained");

    const auto stamp = fs::last_write_time(include);
    writeText(include, "static const uint factor = 7;\n");
    fs::last_write_time(include, stamp);
    const auto changed = compile(compiler, source, secondOptions);
    require(!compiler.lastCompileStatistics().programCacheHit && !compiler.lastCompileStatistics().moduleCacheHit &&
                probe.run(changed) == 63,
            "Same-timestamp include edits reused stale code");

    writeText(root / "early/value.slangh", "static const uint factor = 11;\n");
    const auto shadowed = compile(compiler, source, secondOptions);
    require(!compiler.lastCompileStatistics().programCacheHit && probe.run(shadowed) == 99,
            "New earlier include did not invalidate resolved dependencies");

    auto macroOptions    = secondOptions;
    macroOptions.defines = {{"FEATURE", "20"}};
    const auto macro     = compile(compiler, source, macroOptions);
    require(!compiler.lastCompileStatistics().moduleCacheHit && probe.run(macro) == 119,
            "Macro configuration reused an incompatible frontend");
    const auto original = compile(compiler, source, secondOptions);
    require(probe.run(original) == 99, "Macro selection leaked back into the previous program");

    const auto linked = root / "choices.slang";
    writeText(linked, "export static const uint choice = 2;");
    auto linkedOptions = options;
    linkedOptions.linkSources.clear();
    linkedOptions.linkModules = {fs::absolute(linked)};
    require(probe.run(compile(compiler, source, linkedOptions)) == 22, "Linked file initial output is incorrect");
    writeText(linked, "export static const uint choice = 6;");
    require(probe.run(compile(compiler, source, linkedOptions)) == 66, "Linked file edit reused stale code");

    const auto cacheFile = root / "cache" / (original.compileKey + ".vshadercache");
    const auto goodCache = readBytes(cacheFile);
    writeText(root / "early/value.slangh", "invalid shader input;");
    reject(compiler.compileExpected(source, secondOptions));
    require(readBytes(cacheFile) == goodCache && probe.run(original) == 99,
            "Failed compilation overwrote the last good cache or program");
    writeText(root / "early/value.slangh", "static const uint factor = 11;\n");
    require(probe.run(compile(compiler, source, secondOptions)) == 99, "Compiler recovery failed");

    writeText(cacheFile, "corrupt cache");
    require(probe.run(compile(compiler, source, secondOptions)) == 99 &&
                !compiler.lastCompileStatistics().programCacheHit,
            "Corrupt cache did not explicitly recompile");

    // Force a lookup collision while retaining a valid container checksum.
    auto bytes          = readBytes(cacheFile);
    auto document       = nlohmann::json::from_cbor(bytes.begin() + 16, bytes.end());
    document["request"] = "different canonical request";
    const auto payload  = nlohmann::json::to_cbor(document);
    bytes.resize(16);
    const auto hash = XXH3_64bits(payload.data(), payload.size());
    for (size_t i = 0; i < sizeof(hash); ++i)
    {
        bytes[8 + i] = uint8_t(hash >> (8 * i));
    }
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    const auto rewritten = writeFileAtomicallyExpected(cacheFile, std::as_bytes(std::span(bytes)));
    require(bool(rewritten), rewritten ? "" : rewritten.error().c_str());
    require(probe.run(compile(compiler, source, secondOptions)) == 99 &&
                !compiler.lastCompileStatistics().programCacheHit,
            "Hash collision bypassed request equality");
    reject(ShaderProgram::loadExpected(cacheFile));

    // Malformed requests must return diagnostics and leave the last good program/cache usable.
    const auto unchangedCache = readBytes(cacheFile);
    auto       invalid        = secondOptions;
    invalid.profile           = "spirv_invalid";
    reject(compiler.compileExpected(source, invalid));
    invalid              = secondOptions;
    invalid.capabilities = {"not_a_slang_capability"};
    reject(compiler.compileExpected(source, invalid));
    invalid                  = secondOptions;
    invalid.entries[0].stage = VriShaderStageBits(VriShaderStage_Vertex | VriShaderStage_Compute);
    reject(compiler.compileExpected(source, invalid));
    invalid.entries[0].stage = VriShaderStage_Vertex;
    reject(compiler.compileExpected(source, invalid));
    invalid                 = secondOptions;
    invalid.entries[0].name = "missingEntry";
    reject(compiler.compileExpected(source, invalid));
    invalid              = secondOptions;
    invalid.optimization = ShaderOptimization(-1);
    reject(compiler.compileExpected(source, invalid));
    invalid                  = secondOptions;
    invalid.debugInformation = ShaderDebugInformation(99);
    reject(compiler.compileExpected(source, invalid));
    invalid         = secondOptions;
    invalid.defines = {{"FEATURE", std::string("1\0tail", 6)}};
    reject(compiler.compileExpected(source, invalid));
    invalid = secondOptions;
    invalid.linkSources[0].value += char(0);
    reject(compiler.compileExpected(source, invalid));
    invalid             = secondOptions;
    invalid.linkModules = {root / "absent-linked.slang"};
    reject(compiler.compileExpected(source, invalid));
    reject(compiler.compileExpected(root / "absent.slang", secondOptions));
    reject(compiler.compileSourceExpected({}, text, secondOptions));
    reject(compiler.compileSourceExpected(root / "nul.slang", std::string("bad\0text", 8), {}));
    reject(compiler.compileSourceExpected(root / "utf8.slang", std::string(1, char(0xff)), {}));
    reject(ShaderCompiler::create(std::filesystem::path(std::string("bad\0path", 8))));
    const auto syntaxError = compiler.compileSourceExpected(root / "bad.slang", "this is invalid slang;", {});
    reject(syntaxError);
    require(syntaxError.error().find("bad.slang") != std::string::npos, "Compiler error lost source identity");
    reject(compiler.compileSourceExpected(root / "missing-include.slang", "#include \"absent.slangh\"\n", {}));
    reject(compiler.compileSourceExpected(root / "no-entry.slang", "struct Data { uint value; };", {}));

    constexpr std::string_view emptyKernel  = "[shader(\"compute\")][numthreads(1,1,1)] void main() {}";
    const auto                 emptyProgram = compiler.compileSourceExpected(root / "empty.slang", emptyKernel, {});
    require(bool(emptyProgram) && emptyProgram->shaders.size() == 1,
            "Reflection rejected an entry with no global parameters");
    const auto unavailableCache = root / "cache-is-a-file";
    writeText(unavailableCache, "retained cache destination");
    const auto retainedCacheFile = readBytes(unavailableCache);
    auto       uncachedCompiler  = ShaderCompiler::create(unavailableCache);
    require(bool(uncachedCompiler), "Optional cache path prevented compiler context construction");
    for (uint32_t attempt = 0; attempt < 2; ++attempt)
    {
        const auto uncached = (*uncachedCompiler)->compileSourceExpected(root / "empty.slang", emptyKernel, {});
        require(bool(uncached) && uncached->shaders.front().words == emptyProgram->shaders.front().words &&
                    !(*uncachedCompiler)->lastCompileStatistics().programCacheHit &&
                    (*uncachedCompiler)->lastCompileStatistics().moduleCacheHit == (attempt != 0) &&
                    readBytes(unavailableCache) == retainedCacheFile,
                "Optional cache publication failure lost valid code, IR reuse or destination bytes");
    }
    ShaderCompileOptions webOptions;
    webOptions.profile    = "wgsl";
    const auto webProgram = compiler.compileSourceExpected(root / "empty-web.slang", emptyKernel, webOptions);
    require(bool(webProgram) && !webProgram->shaders.front().wgsl.empty(), "Expected WGSL compilation failed");
    webOptions.rayQuery = true;
    reject(compiler.compileSourceExpected(root / "empty-web.slang", emptyKernel, webOptions));
    const auto recovered = compile(compiler, source, secondOptions);
    require(readBytes(cacheFile) == unchangedCache && compiler.lastCompileStatistics().programCacheHit &&
                probe.run(recovered) == 99,
            "Rejected requests changed the last good program cache or prevented recovery");

    const auto atomicFile = root / "atomic.bin";
    writeText(atomicFile, "old");
    writeText(atomicFile, "replacement");
    require(readBytes(atomicFile) == std::vector<uint8_t>({'r', 'e', 'p', 'l', 'a', 'c', 'e', 'm', 'e', 'n', 't'}),
            "Expected atomic replacement did not publish complete bytes");
    const auto blocked = root / "blocked";
    fs::create_directory(blocked);
    writeText(blocked / "sentinel", "retained");
    const auto retainedSentinel = readBytes(blocked / "sentinel");
    const auto beforeFailure    = std::distance(fs::directory_iterator(root), fs::directory_iterator());
    const std::array<std::byte, 1> payloadByte {std::byte {42}};
    reject(writeFileAtomicallyExpected(blocked, payloadByte));
    reject(writeFileAtomicallyExpected(root / "missing-parent/output.bin", payloadByte));
    require(fs::is_directory(blocked) && readBytes(blocked / "sentinel") == retainedSentinel &&
                std::distance(fs::directory_iterator(root), fs::directory_iterator()) == beforeFailure,
            "Atomic publication failure changed destination or leaked a temporary file");

    constexpr std::string_view gameText   = R"(Shader "Tests/CompilerVariants"
{
    Properties
    {
        gain ("Gain", Float) = 3
    }
    Variant "Low"
    {
        Constants { uint choice = 2 }
    }
    Variant "High"
    {
        Constants { uint choice = 7 }
    }
    SubShader
    {
        Pass
        {
            Name "Experiment"
            Compute main
            SLANGPROGRAM
            extern static const uint choice;
            RWStructuredBuffer<uint> result;
            [numthreads(1, 1, 1)]
            void main(uint3 index : SV_DispatchThreadID)
            {
                result[0] = uint(material.gain) * choice;
            }
            ENDSLANG
        }
    }
})";
    const auto                 gameSource = root / "variants.vshader";
    writeText(gameSource, gameText);
    auto game = ShaderAsset::compile(gameSource);
    require(probe.run(game, "Low") == 6 && probe.run(game, "High") == 21,
            "Game variants reused incompatible code or material reflection");
    const auto gameArtifact = root / "variants.vshaderc";
    game.save(gameArtifact);
    fs::remove(gameSource);
    game = ShaderAsset::load(gameArtifact);
    require(probe.run(game, "High") == 21, "Source-free game variant lost code or material layout");
    require(probe.run(original, false) == 99 && probe.run(original) == 99,
            "Driver pipeline cache changed native compute output");

    std::printf("Compiler GPU checks passed: Expected compile/archive/pipeline, IR/WGSL, disk cache, invalidation and "
                "failure recovery\n");
    std::printf("Measured frontend cold %.1f ms, IR %.1f ms; reopened program cache %.1f ms\n",
                cold.frontendMilliseconds,
                reused.frontendMilliseconds,
                cached.totalMilliseconds);
    return 0;
}
catch (const std::exception& error)
{
    std::fprintf(stderr, "Shader compiler test: %s\n", error.what());
    return 1;
}
