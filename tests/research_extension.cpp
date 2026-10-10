#include <vultra/api/plugin_session.hpp>
#include <vultra/api/research_bridge.hpp>
#include <vultra/core/base/stable_id.hpp>
#include <vultra/servers/rendering/research/capture.hpp>

#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

namespace
{
    using namespace vultra;

    void require(bool value, const char* message)
    {
        if (!value)
        {
            throw std::runtime_error(message);
        }
    }

    struct Extension
    {
        const VultraResearchApi* api            = nullptr;
        VriPipelineLayout*       layout         = nullptr;
        VriDescriptorPool*       pool           = nullptr;
        VriDescriptorSet*        set            = nullptr;
        void*                    shader         = nullptr;
        bool                     failPipeline   = false;
        uint32_t                 pipelineBuilds = 0;
        uint32_t                 stopped        = 0;
        VultraGraphFrame         expired {};
        VultraGraphResource      input {};
        VultraGraphResource      output {};
        const double*            parameters = nullptr;

        static VriPipeline* pipeline(void* context, const VriShaderDesc* shaders, uint32_t count)
        {
            auto& state = *static_cast<Extension*>(context);
            ++state.pipelineBuilds;
            if (state.failPipeline || count != 1)
            {
                return nullptr;
            }
            VriPipeline*                 result = nullptr;
            const VriComputePipelineDesc desc {state.layout, shaders[0], state.api->pipeline_cache};
            check(state.api->core->CreateComputePipeline(state.api->device, &desc, &result), "Test native pipeline");
            return result;
        }

        static VultraStatus execute(void* context, VultraGraphFrame frame, VriCommandBuffer* cmd)
        {
            auto&               state = *static_cast<Extension*>(context);
            const auto&         api   = *state.api;
            VultraResearchFrame cameras {};
            require(api.frame(frame, &cameras) == VULTRA_STATUS_OK && cameras.index == 37, "Native camera snapshot");
            VultraGraphResourceInfo info {};
            require(api.resource_info(frame, state.output, &info) == VULTRA_STATUS_OK, "Native output info");
            VriTexture*    texture = nullptr;
            VriDescriptor* views[2] {};
            require(api.texture(frame, state.input, &texture, &views[0]) == VULTRA_STATUS_OK, "Native input view");
            require(api.texture(frame, state.output, &texture, &views[1]) == VULTRA_STATUS_OK, "Native output view");
            VriDescriptorRangeUpdateDesc updates[2] {};
            for (uint32_t i = 0; i < 2; ++i)
            {
                updates[i].descriptors   = &views[i];
                updates[i].descriptorNum = 1;
            }
            api.core->UpdateDescriptorRanges(state.set, 0, 2, updates);
            api.core->CmdSetPipelineLayout(cmd, state.layout);
            api.core->CmdSetPipeline(cmd, api.pipeline(api.context, state.shader));
            api.core->CmdSetDescriptorSet(cmd, 0, state.set);
            const std::array constants {float(state.parameters[0]), 0.0f, 0.0f, 0.0f};
            api.core->CmdSetConstants(cmd, 0, constants.data(), sizeof(constants));
            const VriDispatchDesc dispatch {(info.texture.width + 7) / 8, (info.texture.height + 7) / 8, 1};
            api.core->CmdDispatch(cmd, &dispatch);
            return VULTRA_STATUS_OK;
        }

        static VultraStatus build(void*                      context,
                                  VultraGraphFrame           frame,
                                  const char*                name,
                                  const VultraGraphResource* inputs,
                                  uint32_t                   inputCount,
                                  const double*              parameters,
                                  uint32_t                   parameterCount,
                                  VultraGraphResource*       outputs,
                                  uint32_t                   outputCount)
        {
            auto& state = *static_cast<Extension*>(context);
            require(inputCount == 1 && parameterCount == 1 && outputCount == 1, "Native pass contract");
            state.expired    = frame;
            state.input      = inputs[0];
            state.parameters = parameters;
            VultraGraphResourceInfo info {};
            require(state.api->resource_info(frame, state.input, &info) == VULTRA_STATUS_OK, "Native source info");
            info.texture.usage =
                VriTextureUsage_ShaderResource | VriTextureUsage_ShaderResourceStorage | VriTextureUsage_TransferSrc;
            require(state.api->create_texture(frame, name, &info.texture, &state.output) == VULTRA_STATUS_OK,
                    "Native texture declaration");
            const VultraGraphUse uses[2] {{state.input, VULTRA_SAMPLED}, {state.output, VULTRA_STORAGE_WRITE}};
            require(state.api->add_pass(frame, name, uses, 2, execute, context, 0) == VULTRA_STATUS_OK,
                    "Native command callback");
            // A foreign graph's resources remain invalid inside a live frame.
            const VultraGraphResource foreign {nullptr, 0};
            require(state.api->resource_info(frame, foreign, &info) != VULTRA_STATUS_OK, "Foreign graph accepted");
            outputs[0] = state.output;
            return VULTRA_STATUS_OK;
        }

        static VultraStatus create(void* context, VultraNativePass* pass)
        {
            *pass = {context,
                     build,
                     [](void* data)
                     {
                         ++static_cast<Extension*>(data)->stopped;
                     }};
            return VULTRA_STATUS_OK;
        }
    };

    void write(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream output(path);
        output.exceptions(std::ios::failbit | std::ios::badbit);
        output << text;
    }
} // namespace

int main()
try
{
    using namespace vultra;
    const auto directory = std::filesystem::absolute("build/.tmp/native-research-" + StableId::generate().toString());
    std::filesystem::create_directories(directory);

    struct Cleanup
    {
        std::filesystem::path path;

        ~Cleanup()
        {
            std::filesystem::remove_all(path);
        }
    } cleanup {directory};

    const auto        source = directory / "gain.slang";
    std::ifstream     original("examples/research/shaders/color_gain.slang");
    const std::string originalShader((std::istreambuf_iterator<char>(original)), {});
    require(!originalShader.empty(), "Shader fixture missing");
    const std::string shader = "#include <sdk_choice.slangh>\n" + originalShader;
    const auto        sdk    = directory / "selected-sdk";
    std::filesystem::create_directories(sdk / "shaders/builtin/shaders");
    std::filesystem::create_directories(sdk / "shaders/external");
    write(sdk / "shaders/builtin/shaders/sdk_choice.slangh", "// Explicit matching SDK.\n");
    std::filesystem::create_directories(directory / "sdk/shaders/builtin/shaders");
    write(directory / "sdk/shaders/builtin/shaders/sdk_choice.slangh", "#error Stale project SDK selected\n");
    write(source, shader);

    Device          device;
    ProjectManifest project;
    project.mainScene       = "scene.vscene";
    const auto shaderId     = project.addAsset("gain.slang");
    const auto graphId      = project.addAsset("method.vgraph");
    const auto comparisonId = project.addAsset("comparison.vgraph");
    project.research        = ResearchProject {"Native test", 17, 19, 0, {{"Reference", graphId}}, comparisonId};
    project.research->rendererSettings = R"({"ibl":false,"lightIntensity":2})";
    project.save(directory / "project.vproject");
    const auto loaded = ProjectManifest::load(directory / "project.vproject");
    require(loaded.research && loaded.research->methods[0].graph == graphId &&
                loaded.research->comparison == comparisonId &&
                loaded.research->rendererSettings == project.research->rendererSettings,
            "Research metadata roundtrip");
    project.research->width  = 6254;
    project.research->height = 2962;
    project.save(directory / "large-eye.vproject");
    const auto largeEye = ProjectManifest::load(directory / "large-eye.vproject");
    require(largeEye.research->width == 6254 && largeEye.research->height == 2962,
            "Measured headset render extent rejected");
    for (const char* invalid : {"[]", R"({"path":1})"})
    {
        project.research->rendererSettings = invalid;
        bool rejected                      = false;
        try
        {
            project.save(directory / "invalid-renderer.vproject");
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        require(rejected, "Invalid research renderer metadata accepted");
    }
    project.research->width            = 17;
    project.research->height           = 19;
    project.research->rendererSettings = loaded.research->rendererSettings;
    project.research->methods.push_back({"Reference", graphId});
    bool rejected = false;
    try
    {
        project.save(directory / "invalid.vproject");
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    require(rejected, "Duplicate method name accepted");
    project.research->methods.pop_back();
    require(project.asset(shaderId).path == "gain.slang", "Shader asset identity");

    AssetSource assets(directory);
    rejected = false;
    try
    {
        ResearchBridge invalid(device, project, assets, directory / "missing-sdk");
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    require(rejected, "Missing explicit SDK accepted");
    ResearchBridge bridge(device, project, assets, sdk);
    Extension      extension;
    extension.api                    = &bridge.api();
    const auto&                  api = bridge.api();
    const VriDescriptorRangeDesc ranges[2] {{0, 1, VriDescriptorType_Texture, VriShaderStage_Compute},
                                            {1, 1, VriDescriptorType_StorageTexture, VriShaderStage_Compute}};
    VriDescriptorSetDesc         set {};
    set.ranges   = ranges;
    set.rangeNum = 2;
    const VriPushConstantDesc push {0, 16, VriShaderStage_Compute};
    VriPipelineLayoutDesc     layout {};
    layout.descriptorSets   = &set;
    layout.descriptorSetNum = 1;
    layout.pushConstants    = &push;
    layout.pushConstantNum  = 1;
    layout.shaderStages     = VriShaderStage_Compute;
    check(device.core.CreatePipelineLayout(device.handle, &layout, &extension.layout), "Native layout");
    VriDescriptorPoolDesc pool {};
    pool.descriptorSetMaxNum  = 1;
    pool.textureMaxNum        = 1;
    pool.storageTextureMaxNum = 1;
    check(device.core.CreateDescriptorPool(device.handle, &pool, &extension.pool), "Native pool");
    check(device.core.AllocateDescriptorSets(extension.pool, extension.layout, 0, &extension.set, 1), "Native set");
    const VultraShaderEntry entry {"gainMain", VriShaderStage_Compute};
    require(
        api.create_shader(api.context, "gain.slang", &entry, 1, Extension::pipeline, &extension, &extension.shader) ==
            VULTRA_STATUS_OK,
        "Native shader load");
    auto* firstPipeline = api.pipeline(api.context, extension.shader);
    require(firstPipeline, "Missing native pipeline");
    // The host changes cwd to an extracted engine pack; project caches must survive its removal.
    const auto cache = directory / ".vultra/shaders";
    require(std::filesystem::is_directory(cache) && std::ranges::any_of(std::filesystem::directory_iterator(cache),
                                                                        [](const auto& item)
                                                                        {
                                                                            return item.path().extension() ==
                                                                                   ".vshadercache";
                                                                        }),
            "Native shader cache must be stored under the project root");
    const VultraPassPort      input {"source", 1, VriFormat_RGBA16_SFLOAT, -1};
    const VultraPassPort      output {"color", 1, VriFormat_RGBA16_SFLOAT, 0};
    const VultraPassParameter gain {"gain", 1, 0, 4};
    const VultraNativePassDefinition
        pass {"test.native_gain", &input, 1, &output, 1, &gain, 1, 0, &extension, Extension::create};
    require(api.register_pass(api.context, &pass) == VULTRA_STATUS_OK, "Native pass registration");
    require(api.pass_definition_size == sizeof(VultraNativePassDefinition), "Native metadata ABI layout mismatch");
    const VultraPassChoice      choices[] {{"Off", 0}, {"On", 2}};
    const VultraPassParameterUi ui {"Quality", "A discrete setting", VULTRA_PASS_CHOICE, choices, 2, 1};
    VultraPassParameter         discrete {"quality", 1, 0, 2, &ui};
    auto                        discretePass = pass;
    discretePass.type                        = "test.discrete";
    discretePass.parameters                  = &discrete;
    require(api.register_pass(api.context, &discretePass) != VULTRA_STATUS_OK,
            "A default absent from the discrete choices was accepted");
    discrete.value = 2;
    require(api.register_pass(api.context, &discretePass) == VULTRA_STATUS_OK, "Discrete metadata registration failed");
    require(bridge.catalog().definition("test.discrete").parameters[0].choices[1].label == "On",
            "Native presentation metadata was not copied");
    require(api.register_editor(api.context, "Research controls") == VULTRA_STATUS_OK && bridge.hasEditorControls(),
            "Project editor registration failed");
    require(api.register_editor(api.context, "Other controls") != VULTRA_STATUS_OK,
            "Multiple project owners for Controls were accepted");
    require(api.register_pass(api.context, &pass) != VULTRA_STATUS_OK, "Duplicate native type accepted");
    bridge.finishRegistration();
    require(api.register_editor(api.context, "Late controls") != VULTRA_STATUS_OK,
            "Project editor registration accepted after initialization");
    require(api.register_pass(api.context, &pass) != VULTRA_STATUS_OK, "Late native registration accepted");
    VultraResearchFrame cameras {};
    cameras.index = 37;
    bridge.setFrame(cameras);
    Texture image(device, colorTexture({17, 19}, VriFormat_RGBA16_SFLOAT));
    {
        // Pass instance/parameter arrays outlive graph callbacks.
        BuiltPass   instance;
        RenderGraph graph(device);
        const auto  texture = graph.importResource("source", image, false);
        graph.addPass("Clear",
                      {{texture, Usage::eColorWrite}},
                      [&](auto* cmd, auto&)
                      {
                          const float color[4] {0.25f, 0.5f, 1, 1};
                          beginColorPass(device, cmd, image.view(), {17, 19}, color);
                          device.core.CmdEndRendering(cmd);
                      });
        const std::array inputs {texture};
        instance = bridge.catalog().build(graph, pass.type, "Gain", inputs);
        graph.exportResource(instance.outputs[0]);
        graph.compile();
        VultraGraphResourceInfo info {};
        require(api.resource_info(extension.expired, extension.input, &info) != VULTRA_STATUS_OK,
                "Expired build frame accepted");
        Frame frame(device);
        auto  render = [&]
        {
            graph.execute(frame.begin());
            frame.submitAndWait();
            return readback(device, graph.getTexture(instance.outputs[0]));
        };
        const auto baseline = render();
        require(baseline.rgba[(18 * 17 + 16) * 4] == 0.25f && baseline.rgba[(18 * 17 + 16) * 4 + 2] == 1,
                "Native compute readback");
        bridge.catalog().setParameters(instance, {{"gain", 2}});
        const auto updated = render();
        require(updated.rgba[(18 * 17 + 16) * 4] == 0.5f && updated.rgba[(18 * 17 + 16) * 4 + 2] == 2,
                "Live native parameter span");
        const auto identities = bridge.shaderIdentities();
        require(identities.size() == 1 && identities.front().spirvHash != 0 && !identities.front().compileKey.empty() &&
                    !identities.front().dependencies.empty(),
                "Active native shader provenance");

        auto pollEdit = [&](const std::string& text)
        {
            write(source, text);
            std::this_thread::sleep_for(std::chrono::milliseconds(120));
            bridge.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(160));
            bridge.poll();
        };
        pollEdit("intentional shader error;");
        require(api.pipeline(api.context, extension.shader) == firstPipeline, "Compilation failure replaced pipeline");
        require(bridge.shaderIdentities().front().compileKey == identities.front().compileKey,
                "Compilation failure replaced provenance");
        extension.failPipeline = true;
        pollEdit(shader + "\n// pipeline failure candidate\n");
        require(api.pipeline(api.context, extension.shader) == firstPipeline, "Builder failure replaced pipeline");
        require(bridge.shaderIdentities().front().spirvHash == identities.front().spirvHash &&
                    bridge.shaderIdentities().front().compileKey == identities.front().compileKey,
                "Builder failure replaced active shader provenance");
        extension.failPipeline = false;
        pollEdit(shader + "\n// recovered candidate\n");
        require(extension.pipelineBuilds >= 3 && api.pipeline(api.context, extension.shader) != firstPipeline,
                "Native shader reload recovery");
        require(compare(render(), updated).mse == 0, "Recovered native output changed");
        require(bridge.shaderIdentities().front().compileKey != identities.front().compileKey,
                "Successful publication did not publish new provenance");
    }
    require(extension.stopped == 1, "Native pass stop lifetime");
    require(api.destroy_shader(api.context, extension.shader) == VULTRA_STATUS_OK, "Native shader release");
    require(api.destroy_shader(api.context, extension.shader) != VULTRA_STATUS_OK, "Stale shader handle accepted");
    device.core.DestroyDescriptorPool(extension.pool);
    device.core.DestroyPipelineLayout(extension.layout);
    std::cout
        << "Native research tests passed: metadata, scopes, graph, parameters, explicit SDK, shader failure/recovery\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
