#include <vultra/assets/shader_asset.hpp>
#include <vultra/platform/os/file.hpp>
#include <vultra/servers/rendering/shader_material.hpp>
#include <vultra/servers/rendering/shader_runtime.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
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

    VriResult VRI_CALL rejectComputePipeline(VriDevice*, const VriComputePipelineDesc*, VriPipeline** pipeline)
    {
        *pipeline = nullptr;
        return VriResult_Failure;
    }

    class ScopedComputePipelineFailure
    {
    public:
        explicit ScopedComputePipelineFailure(vultra::Device& device) :
            m_Device(device),
            m_Create(device.core.CreateComputePipeline)
        {
            device.core.CreateComputePipeline = rejectComputePipeline;
        }

        ~ScopedComputePipelineFailure()
        {
            m_Device.core.CreateComputePipeline = m_Create;
        }

    private:
        vultra::Device&                                     m_Device;
        decltype(VriCoreInterface {}.CreateComputePipeline) m_Create;
    };

    constexpr std::string_view kSource = R"(Shader "Tests/ComputeMaterial"
{
    Properties
    {
        color ("Color", Color) = (0.5, 0.5, 0.5, 1)
        gain ("Gain", Range(0, 1)) = 2
        [Normal] normalMap ("Normal", 2D) = "normal" {}
    }
    SubShader
    {
        Pass
        {
            Name "Experiment"
            Compute computeMain
            SLANGPROGRAM
            RWStructuredBuffer<float4> result;
            [numthreads(1, 1, 1)]
            void computeMain(uint3 index : SV_DispatchThreadID)
            {
                float4 normal = material.normalMap.SampleLevel(material.normalMapSampler, float2(0.5), 0);
                result[0] = float4(material.color.r, material.gain, normal.xy);
            }
            ENDSLANG
        }
    }
})";
} // namespace

int main()
try
{
    using namespace vultra;
    const auto root = std::filesystem::path("build/.tmp/material-gpu") / StableId::generate().toString();
    std::filesystem::create_directories(root);
    const auto source = root / "compute.vshader";
    const auto cooked = root / "compute.vshaderc";
    writeFileAtomically(source, std::as_bytes(std::span(kSource.data(), kSource.size())));
    auto asset = ShaderAsset::compile(source);
    asset.save(cooked);
    std::filesystem::remove(source);
    asset = ShaderAsset::load(cooked);
    Device                  device;
    Buffer                  output(device,
                                   {16, 16, VriBufferUsage_StorageBuffer | VriBufferUsage_TransferSrc, VriMemoryLocation_Device});
    Buffer                  readback(device, {16, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
    VriDescriptor*          view = nullptr;
    const VriBufferViewDesc desc {output.handle, VriDescriptorType_StorageBuffer, VriFormat_Unknown, 0, 16};
    check(device.core.CreateBufferView(device.handle, &desc, &view), "Create material test output");
    MaterialInstance instance;
    ShaderMaterial   material(device, asset, instance);
    bool             missing = false;
    try
    {
        material.prepare("Experiment", {});
    }
    catch (const std::invalid_argument& error)
    {
        missing = std::string_view(error.what()).find("result") != std::string_view::npos;
    }
    require(missing, "Missing shader resource was silently accepted");
    const std::array resources {ShaderResourceViews {"result", VriDescriptorType_StorageBuffer, {view}}};
    Frame            frame(device);
    ShaderMaterial*  activeMaterial = &material;
    auto             run            = [&]
    {
        auto* pipeline = activeMaterial->prepare("Experiment", {}, resources);
        auto* commands = frame.begin();
        output.transition(commands, {VriAccess_ShaderResourceStorageWrite, VriPipelineStage_ComputeShader});
        activeMaterial->bind(commands, "Experiment");
        const VriDispatchDesc dispatch {1, 1, 1};
        device.core.CmdDispatch(commands, &dispatch);
        output.transition(commands, {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
        readback.transition(commands, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
        const VriBufferCopyDesc copy {0, 0, 16};
        device.core.CmdCopyBuffer(commands, readback.handle, output.handle, &copy);
        frame.submitAndWait();
        auto* mapped = device.core.MapBuffer(readback.handle, 0, 16);
        require(mapped != nullptr, "Material readback failed");
        std::array<float, 4> pixel {};
        std::memcpy(pixel.data(), mapped, sizeof(pixel));
        device.core.UnmapBuffer(readback.handle);
        return std::pair(pipeline, pixel);
    };
    const auto first = run();
    require(std::abs(first.second[0] - 0.214041f) < 0.00001f && first.second[1] == 2 && first.second[2] == 0.5f &&
                first.second[3] == 0.5f,
            "Reflected GPU bindings, sRGB upload, Range or normal default is incorrect");
    instance.set(asset, "gain", 3.0f);
    const auto changed = run();
    require(changed.first == first.first && changed.second[1] == 3 && material.pipelineCount() == 1,
            "Property update recreated a pipeline or lost uniform data");
    const auto again = run();
    require(again.first == first.first && again.second == changed.second, "Stable frame changed pipeline or pixels");
    const auto liveSource  = root / "live.vshader";
    auto       writeSource = [&](std::string_view text)
    {
        writeFileAtomically(liveSource, std::as_bytes(std::span(text.data(), text.size())));
    };
    writeSource(kSource);
    {
        ShaderRuntime runtime(device, liveSource);
        const auto    firstId  = runtime.addMaterial(instance);
        const auto    secondId = runtime.addMaterial(instance);
        auto          prepare  = [&](uint32_t, ShaderMaterial& candidate)
        {
            candidate.prepare("Experiment", {}, resources);
        };
        prepare(firstId, runtime.material(firstId));
        prepare(secondId, runtime.material(secondId));
        activeMaterial = &runtime.material(firstId);
        require(run().second[1] == 3, "Live shader initial parameters changed");
        const auto generation     = runtime.generation();
        auto*      previous       = &runtime.material(firstId);
        auto*      secondPrevious = &runtime.material(secondId);
        writeSource("Shader invalid { this is not a shader; }");
        require(!runtime.reload(prepare) && runtime.generation() == generation &&
                    &runtime.material(firstId) == previous && &runtime.material(secondId) == secondPrevious,
                "Compile failure published a partial asset");
        require(run().second[1] == 3, "Compile failure broke the live GPU material");
        writeSource(kSource);
        require(!runtime.reload(
                    [&](uint32_t index, ShaderMaterial& candidate)
                    {
                        prepare(index, candidate);
                        if (index == secondId)
                        {
                            ShaderPassContext invalid;
                            invalid.colors = {VriFormat_RGBA8_UNORM};
                            candidate.prepare("Experiment", invalid, resources);
                        }
                    }) &&
                    runtime.generation() == generation && &runtime.material(firstId) == previous,
                "GPU preparation failure replaced the live asset");
        require(!runtime.reload(
                    [&](uint32_t index, ShaderMaterial& candidate)
                    {
                        if (index == secondId)
                        {
                            // Fail at the VRI creation boundary after the first GPU candidate succeeded.
                            ScopedComputePipelineFailure failure(device);
                            prepare(index, candidate);
                        }
                        else
                        {
                            prepare(index, candidate);
                        }
                    }) &&
                    runtime.generation() == generation && &runtime.material(firstId) == previous &&
                    &runtime.material(secondId) == secondPrevious &&
                    runtime.diagnostics().find("Create shader compute pipeline failed") != std::string::npos,
                "Pipeline creation failure published a partial shader asset");
        require(run().second[1] == 3, "Pipeline creation failure changed the previous GPU material");
        std::string changed(kSource);
        changed.replace(changed.find("Range(0, 1)) = 2"), 16, "Range(0, 1)) = 5");
        changed.insert(changed.find("        [Normal]"), "        extra (\"Extra\", Float) = 7\n");
        writeSource(changed);
        require(runtime.reload(prepare) && runtime.generation() == generation + 1,
                "Shader candidate did not recover after failure");
        activeMaterial = &runtime.material(firstId);
        require(run().second[1] == 3 && std::get<float>(runtime.instance(firstId).value(runtime.asset(), "extra")) == 7,
                "Reload lost matching overrides or new defaults");
        // An edit during candidate preparation must discard every candidate instance.
        const auto recovered = runtime.generation();
        require(!runtime.reload(
                    [&](uint32_t index, ShaderMaterial& candidate)
                    {
                        prepare(index, candidate);
                        if (index == firstId)
                        {
                            writeSource(kSource);
                        }
                    }) &&
                    runtime.generation() == recovered,
                "Source revision changed during preparation but was published");
        const auto deadline  = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        bool       published = false;
        while (!published && std::chrono::steady_clock::now() < deadline)
        {
            published = runtime.poll(prepare);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        require(published && runtime.generation() == recovered + 1,
                "FileWatch recovery did not publish the latest edit");
        activeMaterial = &runtime.material(firstId);
        require(run().second[1] == 3, "Asynchronous reload lost the instance value");
        runtime.removeMaterial(secondId);
        bool removed = false;
        try
        {
            runtime.material(secondId);
        }
        catch (const std::invalid_argument&)
        {
            removed = true;
        }
        require(removed, "Removed shader instance remained accessible");
        require(runtime.reload(prepare), "Reload with a removed instance failed");
        activeMaterial = &runtime.material(firstId);
        require(run().second[1] == 3, "Reloading an instance hole changed the surviving material");
        require(runtime.addMaterial(instance) == secondId, "Removed instance index was not reused");
        prepare(secondId, runtime.material(secondId));
    }
    activeMaterial = &material;
    device.waitIdle();
    device.core.DestroyDescriptor(view);
    std::puts(
        "Shader material GPU passed: source-free compute, reflected sets/resources, defaults, sRGB and stable updates");
    return 0;
}
catch (const std::exception& error)
{
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
