#include "../examples/common/triangle.hpp"

#include <vultra/core/profiling/profiler.hpp>
#include <vultra/core/rhi/swapchain.hpp>
#include <vultra/function/renderer/gui.hpp>
#include <vultra/function/rendergraph/render_graph.hpp>
#include <vultra/function/research/capture.hpp>

#include <GLFW/glfw3.h>

#include <cmath>
#include <fstream>
#include <iostream>
#include <thread>

namespace
{
    void require(bool ok, const char* message)
    {
        if (!ok)
        {
            throw std::runtime_error(message);
        }
    }

    template<class F>
    void reject(F&& f)
    {
        bool rejected = false;
        try
        {
            f();
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        require(rejected, "Expected graph validation error");
    }

    void write(const std::filesystem::path& path, const char* text)
    {
        std::ofstream f(path);
        f << text;
        f.close();
        if (!f)
        {
            throw std::runtime_error("Test shader write failed");
        }
    }
} // namespace
int main()
try
{
    using namespace vultra;
    Device     device;
    Frame      frame(device);
    const auto scratch = std::filesystem::path("build/.tmp/gpu-tests") /
                         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(scratch / "passes");
    std::filesystem::create_directories(scratch / "lib");
    std::ifstream source("examples/research/shaders/triangle.slang");
    std::string   shader((std::istreambuf_iterator<char>(source)), std::istreambuf_iterator<char>());
    const auto    include = shader.find("color.slangh");
    require(include != std::string::npos, "Test shader include missing");
    shader.replace(include, std::string("color.slangh").size(), "lib/color.slangh");
    write(scratch / "passes/triangle.slang", shader.c_str());
    std::filesystem::copy_file("examples/research/shaders/color.slangh", scratch / "lib/color.slangh");
    Triangle    triangle(device, VriFormat_BGRA8_UNORM, scratch / "passes/triangle.slang", scratch, {scratch});
    Profiler    profiler(device);
    RenderGraph graph(device);
    const auto  output        = graph.createTexture("output", colorTexture({129, 73}, VriFormat_BGRA8_UNORM));
    const auto  unused        = graph.createTexture("unused", colorTexture({16, 16}));
    int         discardedRuns = 0;
    graph.addPass("discarded",
                  {{unused, Usage::eColorWrite}},
                  [&](auto*, auto&)
                  {
                      ++discardedRuns;
                  });
    graph.addPass("draw",
                  {{output, Usage::eColorWrite}},
                  [&](auto* cmd, auto& g)
                  {
                      const float clear[4] {0.1f, 0.2f, 0.7f, 0.4f};
                      triangle.draw(cmd, g.getTexture(output), clear);
                  });
    graph.exportResource(output);
    graph.compile();
    require(graph.activePasses() == std::vector<std::string> {"draw"}, "Dead pass culling");
    auto render = [&]
    {
        graph.execute(frame.begin(), &profiler);
        frame.submitAndWait();
        profiler.collect();
        return readback(device, graph.getTexture(output));
    };
    const auto original = render();
    require(discardedRuns == 0, "Culled pass executed");
    require(std::abs(original.rgba[0] - 0.1f) < 0.005f && std::abs(original.rgba[2] - 0.7f) < 0.005f &&
                std::abs(original.rgba[3] - 0.4f) < 0.005f,
            "BGRA order, row pitch, or alpha capture");
    require(!profiler.timings().empty() && profiler.timings()[0].cpuMs >= 0, "Profiler CPU result missing");
    if (profiler.hasGpuTimings())
    {
        require(profiler.timings()[0].gpuMs > 0, "GPU timestamps not resolved");
    }
    auto pollUntil = [&](auto predicate)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!predicate() && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            triangle.pipeline->poll();
        }
        require(predicate(), "FileWatch reload timed out");
    };
    const auto generation  = triangle.pipeline->generation();
    const auto oldPipeline = triangle.pipeline->handle();
    std::cerr << "[Test] Expected shader error follows: checking that the previous pipeline survives.\n";
    write(scratch / "lib/color.slangh", "this is an intentional syntax error;\n");
    pollUntil(
        [&]
        {
            return !triangle.pipeline->diagnostics().empty();
        });
    require(triangle.pipeline->handle() == oldPipeline && triangle.pipeline->generation() == generation,
            "Bad shader replaced working pipeline");
    require(compare(original, render()).mse == 0, "Failed shader reload changed rendered pixels");
    write(scratch / "lib/color.slangh", "float3 experimentColor(float3 c) { return float3(0.9,0.1,0.2); }\n");
    pollUntil(
        [&]
        {
            return triangle.pipeline->generation() > generation;
        });
    require(compare(original, render()).mse > 0.001, "Dependency hotreload did not change image");

    RenderGraph bad(device);
    const auto  uninitialized = bad.createTexture("never written", colorTexture({16, 16}));
    bad.addPass(
        "read",
        {{uninitialized, Usage::eSampled}},
        [](auto*, auto&)
        {
        },
        true);
    reject(
        [&]
        {
            bad.compile();
        });
    reject(
        [&]
        {
            bad.exportResource(output);
        });
    RenderGraph duplicate(device);
    const auto  twice = duplicate.createTexture("twice", colorTexture({16, 16}));
    duplicate.addPass(
        "twice",
        {{twice, Usage::eColorWrite}, {twice, Usage::eSampled}},
        [](auto*, auto&)
        {
        },
        true);
    reject(
        [&]
        {
            duplicate.compile();
        });

    RenderGraph wrongUsage(device);
    const auto  colorOnly = wrongUsage.createTexture("missing storage flag", colorTexture({16, 16}));
    wrongUsage.addPass(
        "invalid storage write",
        {{colorOnly, Usage::eStorageWrite}},
        [](auto*, auto&)
        {
        },
        true);
    reject(
        [&]
        {
            wrongUsage.compile();
        });

    // Test real buffer producer/consumer copies, not just graph bookkeeping.
    Buffer upload(device, {sizeof(uint32_t), 0, VriBufferUsage_TransferSrc, VriMemoryLocation_HostUpload});
    Buffer readbackBuffer(device, {sizeof(uint32_t), 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
    auto*  mapped = device.core.MapBuffer(upload.handle, 0, sizeof(uint32_t));
    require(mapped != nullptr, "Map upload");
    *static_cast<uint32_t*>(mapped) = 0x12345678;
    device.core.UnmapBuffer(upload.handle);
    RenderGraph buffers(device);
    const auto  src = buffers.importResource("upload", upload);
    const auto  tmp = buffers.createBuffer(
        "temporary",
        {sizeof(uint32_t), 0, VriBufferUsage_TransferSrc | VriBufferUsage_TransferDst, VriMemoryLocation_Device});
    const auto dst = buffers.importResource("readback", readbackBuffer, false);
    buffers.addPass("upload",
                    {{src, Usage::eCopySource}, {tmp, Usage::eCopyDestination}},
                    [&](auto* cmd, auto& g)
                    {
                        const VriBufferCopyDesc copy {0, 0, sizeof(uint32_t)};
                        device.core.CmdCopyBuffer(cmd, g.getBuffer(tmp).handle, upload.handle, &copy);
                    });
    buffers.addPass("readback",
                    {{tmp, Usage::eCopySource}, {dst, Usage::eCopyDestination}},
                    [&](auto* cmd, auto& g)
                    {
                        const VriBufferCopyDesc copy {0, 0, sizeof(uint32_t)};
                        device.core.CmdCopyBuffer(cmd, readbackBuffer.handle, g.getBuffer(tmp).handle, &copy);
                    });
    buffers.exportResource(dst);
    buffers.compile();
    buffers.execute(frame.begin());
    frame.submitAndWait();
    mapped = device.core.MapBuffer(readbackBuffer.handle, 0, sizeof(uint32_t));
    require(mapped != nullptr, "Map buffer result");
    const auto value = *static_cast<uint32_t*>(mapped);
    device.core.UnmapBuffer(readbackBuffer.handle);
    require(value == 0x12345678, "Graph buffer dependency/copy");

    Texture hdr(device, colorTexture({65, 17}, VriFormat_RGBA16_SFLOAT));
    auto*   cmd = frame.begin();
    hdr.transition(cmd,
                   {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
    const float clear[4] {2, -0.5f, 0.25f, 0.5f};
    beginColorPass(device, cmd, hdr.view(), {65, 17}, clear);
    device.core.CmdEndRendering(cmd);
    frame.submitAndWait();
    const auto hdrImage = readback(device, hdr);
    require(hdrImage.rgba[0] == 2 && hdrImage.rgba[1] == -0.5f && hdrImage.rgba[3] == 0.5f,
            "HDR readback was quantized or lost alpha");

    Window    window("Vultra - verification", {640, 360});
    Swapchain swapchain(device, window, VriFormat_BGRA8_UNORM);
    Gui       gui(device, window, swapchain.format(), {.persistLayout = false});
    for (int i = 0; i < 4; ++i)
    {
        if (i == 2)
        {
            glfwSetWindowSize(window.handle(), 800, 450);
        }
        window.poll();
        gui.begin();
        ImGui::SetNextWindowSize(ImVec2(420, 220));
        const auto guiOrigin = ImGui::GetMainViewport()->Pos;
        ImGui::SetNextWindowPos(ImVec2(guiOrigin.x + 24, guiOrigin.y + 24));
        ImGui::Begin("Vultra verification");
        ImGui::TextUnformatted("VRI + ImGui + RenderGraph + Slang");
        ImGui::Button("Research controls");
        ImGui::End();
        gui.upload(window.framebufferSize());
        auto* target = swapchain.acquire();
        if (!target)
        {
            --i;
            continue;
        }
        cmd = frame.begin();
        target->transition(
            cmd,
            {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        gui.copy(cmd);
        const float bg[4] {0.05f, 0.07f, 0.1f, 1};
        beginColorPass(device, cmd, target->view(), swapchain.size(), bg);
        gui.draw(cmd);
        device.core.CmdEndRendering(cmd);
        target->transition(cmd, {VriAccess_None, VriLayout_Present, VriPipelineStage_AllCommands});
        frame.submitAndWait();
        if (i == 3)
        {
            savePng(readback(device, *target), "build/.tmp/gui-verification.png");
        }
        swapchain.present();
        gui.renderPlatformWindows();
    }
    require(swapchain.size().width == 800 && swapchain.size().height == 450, "Resize was not applied");
    std::cout << "GPU tests passed: draw/readback, graph culling/validation/buffers, profiler, FileWatch "
                 "failure/recovery, HDR, ImGui, resize\n";
    return 0;
}
catch (const std::exception& e)
{
    std::cerr << e.what() << '\n';
    return 1;
}
