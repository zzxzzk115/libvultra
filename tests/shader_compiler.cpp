#include "support/check.hpp"

#include <vultra/core/base/stable_id.hpp>
#include <vultra/gpu/resources.hpp>
#include <vultra/platform/os/file.hpp>
#include <vultra/shader/shader_compiler.hpp>
#include <vultra/shader/shader_pipeline.hpp>

#include <nlohmann/json.hpp>
#include <xxhash.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace
{
    using support::must;
    using support::require;

    void writeText(const std::filesystem::path& file, std::string_view text)
    {
        must(vultra::writeFileAtomically(file, std::as_bytes(std::span(text.data(), text.size()))));
    }

    std::vector<uint8_t> readBytes(const std::filesystem::path& file)
    {
        std::ifstream input(file, std::ios::binary);
        require(bool(input), "Open compiler test artifact");
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    constexpr std::string_view kRejected = "Expected compiler boundary rejection";

    class ComputeProbe
    {
    public:
        explicit ComputeProbe(const std::filesystem::path& root) :
            m_Device(must(vultra::Device::create())),
            m_File(root / "probe.vshaderc"),
            m_Frame(must(vultra::Frame::create(*m_Device))),
            m_Output(must(vultra::Buffer::create(
                *m_Device,
                {4, 4, VriBufferUsage_StorageBuffer | VriBufferUsage_TransferSrc, VriMemoryLocation_Device}))),
            m_Readback(must(
                vultra::Buffer::create(*m_Device, {4, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback})))
        {
            // A failed step ends the test; the destructor releases whatever was created.
            const VriDescriptorRangeDesc range {0, 1, VriDescriptorType_StorageBuffer, VriShaderStage_Compute};
            const VriDescriptorSetDesc   set {0, &range, 1};
            const VriPipelineLayoutDesc  layout {&set, 1, nullptr, 0, VriShaderStage_Compute};
            must(vultra::check(m_Device->core.CreatePipelineLayout(m_Device->handle, &layout, &m_Layout),
                               "Create probe layout"));
            VriDescriptorPoolDesc pool {};
            pool.descriptorSetMaxNum = 1;
            pool.storageBufferMaxNum = 1;
            must(vultra::check(m_Device->core.CreateDescriptorPool(m_Device->handle, &pool, &m_Pool),
                               "Create probe pool"));
            must(vultra::check(m_Device->core.AllocateDescriptorSets(m_Pool, m_Layout, 0, &m_Set, 1),
                               "Allocate probe set"));
            const VriBufferViewDesc view {m_Output.handle, VriDescriptorType_StorageBuffer, VriFormat_Unknown, 0, 4};
            must(vultra::check(m_Device->core.CreateBufferView(m_Device->handle, &view, &m_View), "Create probe view"));
            const VriDescriptor*               descriptor = m_View;
            const VriDescriptorRangeUpdateDesc update {&descriptor, 1, 0};
            m_Device->core.UpdateDescriptorRanges(m_Set, 0, 1, &update);
        }

        ~ComputeProbe()
        {
            release();
        }

        uint32_t run(const vultra::ShaderProgram& program, bool useDriverCache = true)
        {
            must(program.save(m_File));
            const auto pipeline = must(vultra::ShaderPipeline::create(
                *m_Device,
                m_File,
                std::vector<vultra::ShaderEntry> {{"main", VriShaderStage_Compute}},
                [&](std::span<const VriShaderDesc> shaders) -> std::expected<VriPipeline*, vultra::Error>
                {
                    const VriComputePipelineDesc desc {m_Layout,
                                                       shaders.front(),
                                                       useDriverCache ? m_Device->pipelineCache : nullptr};
                    VriPipeline*                 result = nullptr;
                    if (auto created =
                            vultra::check(m_Device->core.CreateComputePipeline(m_Device->handle, &desc, &result),
                                          "Create probe pipeline");
                        !created)
                    {
                        return std::unexpected(created.error());
                    }
                    return result;
                }));
            auto*      commands = must(m_Frame->begin());
            m_Output.transition(commands, {VriAccess_ShaderResourceStorageWrite, VriPipelineStage_ComputeShader});
            m_Device->core.CmdSetPipelineLayout(commands, m_Layout);
            m_Device->core.CmdSetPipeline(commands, pipeline->handle());
            m_Device->core.CmdSetDescriptorSet(commands, 0, m_Set);
            return dispatchAndRead(commands);
        }

    private:
        uint32_t dispatchAndRead(VriCommandBuffer* commands)
        {
            const VriDispatchDesc dispatch {1, 1, 1};
            m_Device->core.CmdDispatch(commands, &dispatch);
            m_Output.transition(commands, {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
            m_Readback.transition(commands, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
            const VriBufferCopyDesc copy {0, 0, 4};
            m_Device->core.CmdCopyBuffer(commands, m_Readback.handle, m_Output.handle, &copy);
            must(m_Frame->submitAndWait());
            auto* mapped = m_Device->core.MapBuffer(m_Readback.handle, 0, 4);
            require(mapped != nullptr, "Map compiler probe");
            uint32_t value = 0;
            std::memcpy(&value, mapped, sizeof(value));
            m_Device->core.UnmapBuffer(m_Readback.handle);
            return value;
        }

        void release()
        {
            m_Device->waitIdleForDestruction();
            if (m_Pool)
            {
                m_Device->core.DestroyDescriptorPool(m_Pool);
            }
            if (m_View)
            {
                m_Device->core.DestroyDescriptor(m_View);
            }
            if (m_Layout)
            {
                m_Device->core.DestroyPipelineLayout(m_Layout);
            }
        }

        std::unique_ptr<vultra::Device> m_Device;
        std::filesystem::path           m_File;
        std::unique_ptr<vultra::Frame>  m_Frame;
        vultra::Buffer                  m_Output;
        vultra::Buffer                  m_Readback;
        VriPipelineLayout*              m_Layout = nullptr;
        VriDescriptorPool*              m_Pool   = nullptr;
        VriDescriptorSet*               m_Set    = nullptr;
        VriDescriptor*                  m_View   = nullptr;
    };
} // namespace

int main()
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
    auto         compiler      = must(ShaderCompiler::create(root / "cache"));
    ComputeProbe probe(root);
    const auto   first = must(compiler.compile(source, options));
    const auto   cold  = compiler.lastCompileStatistics();
    require(!cold.programCacheHit && !cold.moduleCacheHit && probe.run(first) == 15, "Cold shader output is incorrect");

    auto secondOptions                 = options;
    secondOptions.linkSources[0].value = "export static const uint choice = 9;";
    const auto second                  = must(compiler.compile(source, secondOptions));
    const auto reused                  = compiler.lastCompileStatistics();
    require(!reused.programCacheHit && reused.moduleCacheHit && probe.run(second) == 27,
            "Reused module leaked a prior variant's link constants");
    require(must(first.resourceBindings()).size() == must(second.resourceBindings()).size() &&
                must(second.resourceBindings()).front().binding == 0 && second.shaders.front().threadGroup[0] == 1,
            "Reused IR lost target resource/entry reflection");

    auto       reopened = must(ShaderCompiler::create(root / "cache"));
    const auto warm     = must(reopened.compile(source, secondOptions));
    const auto cached   = reopened.lastCompileStatistics();
    require(cached.programCacheHit && cached.frontendMilliseconds == 0 && cached.codegenMilliseconds == 0 &&
                warm.shaders.front().words == second.shaders.front().words && probe.run(warm) == 27,
            "Reopened program cache compiled again or changed GPU output");

    const auto stamp = fs::last_write_time(include);
    writeText(include, "static const uint factor = 7;\n");
    fs::last_write_time(include, stamp);
    const auto changed = must(compiler.compile(source, secondOptions));
    require(!compiler.lastCompileStatistics().programCacheHit && !compiler.lastCompileStatistics().moduleCacheHit &&
                probe.run(changed) == 63,
            "Same-timestamp include edits reused stale code");

    writeText(root / "early/value.slangh", "static const uint factor = 11;\n");
    const auto shadowed = must(compiler.compile(source, secondOptions));
    require(!compiler.lastCompileStatistics().programCacheHit && probe.run(shadowed) == 99,
            "New earlier include did not invalidate resolved dependencies");

    auto macroOptions    = secondOptions;
    macroOptions.defines = {{"FEATURE", "20"}};
    const auto macro     = must(compiler.compile(source, macroOptions));
    require(!compiler.lastCompileStatistics().moduleCacheHit && probe.run(macro) == 119,
            "Macro configuration reused an incompatible frontend");
    const auto original = must(compiler.compile(source, secondOptions));
    require(probe.run(original) == 99, "Macro selection leaked back into the previous program");

    const auto linked = root / "choices.slang";
    writeText(linked, "export static const uint choice = 2;");
    auto linkedOptions = options;
    linkedOptions.linkSources.clear();
    linkedOptions.linkModules = {fs::absolute(linked)};
    require(probe.run(must(compiler.compile(source, linkedOptions))) == 22, "Linked file initial output is incorrect");
    writeText(linked, "export static const uint choice = 6;");
    require(probe.run(must(compiler.compile(source, linkedOptions))) == 66, "Linked file edit reused stale code");

    const auto cacheFile = root / "cache" / (original.compileKey + ".vshadercache");
    const auto goodCache = readBytes(cacheFile);
    writeText(root / "early/value.slangh", "invalid shader input;");
    support::rejects(compiler.compile(source, secondOptions), kRejected);
    require(readBytes(cacheFile) == goodCache && probe.run(original) == 99,
            "Failed compilation overwrote the last good cache or program");
    writeText(root / "early/value.slangh", "static const uint factor = 11;\n");
    require(probe.run(must(compiler.compile(source, secondOptions))) == 99, "Compiler recovery failed");

    writeText(cacheFile, "corrupt cache");
    require(probe.run(must(compiler.compile(source, secondOptions))) == 99 &&
                !compiler.lastCompileStatistics().programCacheHit,
            "Corrupt cache did not explicitly recompile");

    // Force a lookup collision while retaining a valid container checksum.
    auto bytes    = readBytes(cacheFile);
    auto document = nlohmann::json::from_cbor(bytes.begin() + 16, bytes.end(), true, false);
    require(!document.is_discarded(), "Program cache payload is not CBOR");
    document["request"] = "different canonical request";
    const auto payload  = nlohmann::json::to_cbor(document);
    bytes.resize(16);
    const auto hash = XXH3_64bits(payload.data(), payload.size());
    for (size_t i = 0; i < sizeof(hash); ++i)
    {
        bytes[8 + i] = uint8_t(hash >> (8 * i));
    }
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    must(writeFileAtomically(cacheFile, std::as_bytes(std::span(bytes))));
    require(probe.run(must(compiler.compile(source, secondOptions))) == 99 &&
                !compiler.lastCompileStatistics().programCacheHit,
            "Hash collision bypassed request equality");
    support::rejects(ShaderProgram::load(cacheFile), kRejected);

    require(probe.run(original, false) == 99 && probe.run(original) == 99,
            "Driver pipeline cache changed native compute output");

    std::printf(
        "Compiler GPU checks passed: IR variants, disk cache, include/macro/link invalidation and failure recovery\n");
    std::printf("Measured frontend cold %.1f ms, IR %.1f ms; reopened program cache %.1f ms\n",
                cold.frontendMilliseconds,
                reused.frontendMilliseconds,
                cached.totalMilliseconds);
    return 0;
}
