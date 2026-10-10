#include <vultra/api/research_bridge.hpp>
#include <vultra/core/base/logger.hpp>

#include <algorithm>
#include <stdexcept>

namespace vultra
{
    namespace
    {
        template<class Callback>
        VultraStatus guarded(Callback callback)
        {
            try
            {
                callback();
                return VULTRA_STATUS_OK;
            }
            catch (const std::exception& error)
            {
                Logger::app().error("Research extension: {}", error.what());
                return VULTRA_STATUS_INVALID_ARGUMENT;
            }
        }
    } // namespace

    struct ResearchBridge::Scope
    {
        ResearchBridge& owner;

        explicit Scope(ResearchBridge& bridge, RenderGraph& graph, bool executing = false) :
            owner(bridge)
        {
            if (owner.m_ActiveGraph)
            {
                throw std::logic_error("Nested native graph scope");
            }
            owner.m_ActiveGraph = &graph;
            owner.m_Executing   = executing;
            ++owner.m_Serial;
        }

        ~Scope()
        {
            owner.m_ActiveGraph = nullptr;
            owner.m_Executing   = false;
        }

        VultraGraphFrame frame() const
        {
            return {&owner, owner.m_Serial};
        }
    };

    class ResearchBridge::NativePass final : public GraphPass
    {
    public:
        NativePass(ResearchBridge& bridge, const VultraNativePassDefinition& definition) :
            m_Bridge(bridge),
            m_OutputCount(definition.output_count)
        {
            if (definition.create(definition.user_data, &m_Pass) != VULTRA_STATUS_OK || !m_Pass.build || !m_Pass.stop)
            {
                if (m_Pass.stop)
                {
                    m_Pass.stop(m_Pass.user_data);
                }
                throw std::runtime_error("Create native graph pass");
            }
        }

        ~NativePass() override
        {
            m_Pass.stop(m_Pass.user_data);
        }

        std::vector<RenderGraph::Resource> addPasses(RenderGraph&                           graph,
                                                     std::string_view                       name,
                                                     std::span<const RenderGraph::Resource> inputs,
                                                     std::span<const double>                parameters) override
        {
            for (const auto input : inputs)
            {
                m_Inputs.push_back(resource(input));
            }
            std::vector<VultraGraphResource> outputs(m_OutputCount);
            Scope                            scope(m_Bridge, graph);
            const std::string                label(name);
            if (m_Pass.build(m_Pass.user_data,
                             scope.frame(),
                             label.c_str(),
                             m_Inputs.data(),
                             uint32_t(m_Inputs.size()),
                             parameters.data(),
                             uint32_t(parameters.size()),
                             outputs.data(),
                             m_OutputCount) != VULTRA_STATUS_OK)
            {
                throw std::runtime_error("Build native pass " + label);
            }
            std::vector<RenderGraph::Resource> result;
            for (const auto output : outputs)
            {
                result.push_back(resource(output));
            }
            return result;
        }

    private:
        ResearchBridge&                  m_Bridge;
        uint32_t                         m_OutputCount;
        VultraNativePass                 m_Pass {};
        std::vector<VultraGraphResource> m_Inputs;
    };

    ResearchBridge& ResearchBridge::access(VultraGraphFrame frame)
    {
        if (!frame.context)
        {
            throw std::invalid_argument("Missing graph frame");
        }
        auto& bridge = *static_cast<ResearchBridge*>(frame.context);
        if (!bridge.m_ActiveGraph || bridge.m_Serial != frame.serial)
        {
            throw std::invalid_argument("Expired graph frame");
        }
        return bridge;
    }

    RenderGraph::Resource ResearchBridge::resource(VultraGraphResource value)
    {
        return {static_cast<const RenderGraph*>(value.graph), value.index};
    }

    VultraGraphResource ResearchBridge::resource(RenderGraph::Resource value)
    {
        return {value.graph, value.index};
    }

    ResearchBridge::ResearchBridge(Device&                device,
                                   const ProjectManifest& project,
                                   const AssetSource&     source,
                                   std::filesystem::path  sdkDirectory) :
        m_Device(device),
        m_Project(project),
        m_Source(source),
        m_Catalog(device)
    {
        if (!sdkDirectory.empty())
        {
            m_ShaderRoot = std::filesystem::absolute(sdkDirectory) / "shaders";
            if (!std::filesystem::is_directory(m_ShaderRoot / "builtin/shaders") ||
                !std::filesystem::is_directory(m_ShaderRoot / "external"))
            {
                throw std::invalid_argument("SDK shader roots are missing: " + sdkDirectory.string());
            }
        }
        else
        {
            m_ShaderRoot = m_Source.root() / "sdk/shaders";
            if (!std::filesystem::is_directory(m_ShaderRoot))
            {
                m_ShaderRoot = std::filesystem::current_path() / "sdk/shaders";
            }
        }
        m_Api.version              = VULTRA_ABI_VERSION;
        m_Api.struct_size          = sizeof(m_Api);
        m_Api.context              = this;
        m_Api.device               = device.handle;
        m_Api.core                 = &device.core;
        m_Api.core_size            = sizeof(device.core);
        m_Api.pipeline_cache       = device.pipelineCache;
        m_Api.pass_definition_size = sizeof(VultraNativePassDefinition);
        m_Api.register_editor      = [](void* context, const char* name)
        {
            return guarded(
                [&]
                {
                    auto& bridge = *static_cast<ResearchBridge*>(context);
                    if (!bridge.m_Registering || !name || !*name || !bridge.m_EditorName.empty())
                    {
                        throw std::invalid_argument("Register one project editor during initialization");
                    }
                    bridge.m_EditorName = name;
                });
        };
        m_Api.get_interface = [](void* context, const char* name, size_t size, void* table)
        {
            return vriGetInterface(static_cast<ResearchBridge*>(context)->m_Device.handle, name, size, table);
        };
        m_Api.register_pass = [](void* context, const VultraNativePassDefinition* definition)
        {
            return guarded(
                [&]
                {
                    if (!definition)
                    {
                        throw std::invalid_argument("Missing pass definition");
                    }
                    static_cast<ResearchBridge*>(context)->registerPass(*definition);
                });
        };
        m_Api.resource_info = [](VultraGraphFrame frame, VultraGraphResource value, VultraGraphResourceInfo* output)
        {
            return guarded(
                [&]
                {
                    if (!output)
                    {
                        throw std::invalid_argument("Missing resource info output");
                    }
                    const auto info = access(frame).m_ActiveGraph->resourceInfo(resource(value));
                    *output         = {uint8_t(info.isTexture), info.textureDesc, info.bufferDesc};
                });
        };
        m_Api.create_texture =
            [](VultraGraphFrame frame, const char* name, const VriTextureDesc* desc, VultraGraphResource* output)
        {
            return guarded(
                [&]
                {
                    if (!name || !desc || !output)
                    {
                        throw std::invalid_argument("Missing texture argument");
                    }
                    *output = resource(access(frame).m_ActiveGraph->createTexture(name, *desc));
                });
        };
        m_Api.create_buffer =
            [](VultraGraphFrame frame, const char* name, const VriBufferDesc* desc, VultraGraphResource* output)
        {
            return guarded(
                [&]
                {
                    if (!name || !desc || !output)
                    {
                        throw std::invalid_argument("Missing buffer argument");
                    }
                    *output = resource(access(frame).m_ActiveGraph->createBuffer(name, *desc));
                });
        };
        m_Api.add_pass = [](VultraGraphFrame      frame,
                            const char*           name,
                            const VultraGraphUse* inputs,
                            uint32_t              count,
                            VultraGraphExecute    execute,
                            void*                 context,
                            uint8_t               sideEffect)
        {
            return guarded(
                [&]
                {
                    if (!name || !execute || (count && !inputs))
                    {
                        throw std::invalid_argument("Missing pass argument");
                    }
                    auto&                         bridge = access(frame);
                    std::vector<RenderGraph::Use> uses;
                    for (uint32_t i = 0; i < count; ++i)
                    {
                        if (inputs[i].usage < VULTRA_COLOR_WRITE || inputs[i].usage > VULTRA_PRESENT)
                        {
                            throw std::invalid_argument("Invalid resource usage");
                        }
                        uses.push_back({resource(inputs[i].resource), Usage(inputs[i].usage)});
                    }
                    bridge.m_ActiveGraph->addPass(
                        name,
                        std::span(uses),
                        [&bridge, execute, context](auto* cmd, auto& graph)
                        {
                            Scope scope(bridge, graph, true);
                            if (execute(context, scope.frame(), cmd) != VULTRA_STATUS_OK)
                            {
                                throw std::runtime_error("Execute native graph pass");
                            }
                        },
                        sideEffect != 0);
                });
        };
        m_Api.texture = [](VultraGraphFrame frame, VultraGraphResource value, VriTexture** handle, VriDescriptor** view)
        {
            return guarded(
                [&]
                {
                    if (!handle || !view)
                    {
                        throw std::invalid_argument("Missing texture output");
                    }
                    auto& texture = access(frame).m_ActiveGraph->getTexture(resource(value));
                    *handle       = texture.handle;
                    *view         = texture.view();
                });
        };
        m_Api.buffer = [](VultraGraphFrame frame, VultraGraphResource value, VriBuffer** output)
        {
            return guarded(
                [&]
                {
                    if (!output)
                    {
                        throw std::invalid_argument("Missing buffer output");
                    }
                    *output = access(frame).m_ActiveGraph->getBuffer(resource(value)).handle;
                });
        };
        m_Api.frame = [](VultraGraphFrame frame, VultraResearchFrame* output)
        {
            return guarded(
                [&]
                {
                    if (!output)
                    {
                        throw std::invalid_argument("Missing camera output");
                    }
                    auto& bridge = access(frame);
                    if (!bridge.m_Executing)
                    {
                        throw std::logic_error("Camera snapshot is available during execution");
                    }
                    *output = bridge.m_Frame;
                });
        };
        m_Api.create_shader = [](void*                    context,
                                 const char*              path,
                                 const VultraShaderEntry* entries,
                                 uint32_t                 count,
                                 VultraPipelineBuilder    builder,
                                 void*                    data,
                                 void**                   output)
        {
            return guarded(
                [&]
                {
                    if (!path || !entries || !count || !builder || !output)
                    {
                        throw std::invalid_argument("Missing shader argument");
                    }
                    auto& bridge   = *static_cast<ResearchBridge*>(context);
                    auto  relative = std::filesystem::path(path);
                    if (!bridge.m_Source.contains(relative))
                    {
                        relative.replace_extension(".vshaderc");
                    }
                    const auto declared = std::ranges::find(bridge.m_Project.assets(), relative, &ProjectAsset::path);
                    if (declared == bridge.m_Project.assets().end())
                    {
                        throw std::invalid_argument("Shader must be a declared project asset: " + std::string(path));
                    }
                    ShaderCompileOptions options;
                    options.rayQuery = (bridge.m_Device.features & VriFeature_RayQuery) != 0;
                    for (uint32_t i = 0; i < count; ++i)
                    {
                        if (!entries[i].name)
                        {
                            throw std::invalid_argument("Missing shader entry");
                        }
                        options.entries.push_back({entries[i].name, entries[i].stage});
                    }
                    // An authoring SDK has stable paths, preserving cross-process program-cache keys.
                    options.includeDirectories = {bridge.m_Source.root(),
                                                  bridge.m_ShaderRoot / "builtin/shaders",
                                                  bridge.m_ShaderRoot / "external"};
                    auto pipeline              = std::make_unique<ShaderPipeline>(
                        bridge.m_Device,
                        bridge.m_Source.materialize(relative),
                        options,
                        [builder, data](auto shaders)
                        {
                            return builder(data, shaders.data(), uint32_t(shaders.size()));
                        },
                        std::filesystem::path {},
                        bridge.m_Source.root() / ".vultra/shaders");
                    *output = pipeline.get();
                    bridge.m_Shaders.push_back(std::move(pipeline));
                });
        };
        m_Api.pipeline = [](void* context, void* handle) -> VriPipeline*
        {
            VriPipeline* output = nullptr;
            guarded(
                [&]
                {
                    output = static_cast<ResearchBridge*>(context)->shader(handle).handle();
                });
            return output;
        };
        m_Api.destroy_shader = [](void* context, void* handle)
        {
            return guarded(
                [&]
                {
                    auto& bridge = *static_cast<ResearchBridge*>(context);
                    bridge.shader(handle);
                    std::erase_if(bridge.m_Shaders,
                                  [handle](const auto& entry)
                                  {
                                      return entry.get() == handle;
                                  });
                });
        };
        m_Api.log = [](void*, const char* message)
        {
            if (message)
            {
                Logger::app().info("[Project] {}", message);
            }
        };
    }

    ResearchBridge::~ResearchBridge()
    {
        m_Device.waitIdle();
    }

    const VultraResearchApi& ResearchBridge::api() const
    {
        return m_Api;
    }

    PassCatalog& ResearchBridge::catalog()
    {
        return m_Catalog;
    }

    void ResearchBridge::finishRegistration()
    {
        m_Registering = false;
    }

    void ResearchBridge::setFrame(const VultraResearchFrame& frame)
    {
        m_Frame = frame;
    }

    void ResearchBridge::poll()
    {
        for (auto& shader : m_Shaders)
        {
            shader->poll();
        }
    }

    std::string ResearchBridge::diagnostics() const
    {
        std::string result;
        for (const auto& shader : m_Shaders)
        {
            if (!shader->diagnostics().empty())
            {
                result += shader->diagnostics() + "\n";
            }
        }
        return result;
    }

    std::vector<ShaderPipelineIdentity> ResearchBridge::shaderIdentities() const
    {
        std::vector<ShaderPipelineIdentity> result;
        for (const auto& shader : m_Shaders)
        {
            const auto& identity = shader->identity();
            if (!std::ranges::any_of(result,
                                     [&](const auto& previous)
                                     {
                                         return previous.file == identity.file &&
                                                previous.spirvHash == identity.spirvHash;
                                     }))
            {
                result.push_back(identity);
            }
        }
        return result;
    }

    ShaderPipeline& ResearchBridge::shader(void* handle)
    {
        const auto found = std::ranges::find_if(m_Shaders,
                                                [handle](const auto& value)
                                                {
                                                    return value.get() == handle;
                                                });
        if (found == m_Shaders.end())
        {
            throw std::invalid_argument("Unknown project shader handle");
        }
        return **found;
    }

    void ResearchBridge::setEditor(const VultraResearchEditorApi* editor)
    {
        m_Api.editor = editor;
    }

    bool ResearchBridge::hasEditorControls() const
    {
        return !m_EditorName.empty();
    }

    void ResearchBridge::registerPass(const VultraNativePassDefinition& input)
    {
        if (!m_Registering)
        {
            throw std::logic_error("Pass registration has ended");
        }
        if (!input.type || !input.create || (input.input_count && !input.inputs) ||
            (input.output_count && !input.outputs) || (input.parameter_count && !input.parameters))
        {
            throw std::invalid_argument("Incomplete native pass definition");
        }
        PassDefinition definition;
        definition.type = input.type;
        auto ports      = [](const VultraPassPort* values, uint32_t count)
        {
            std::vector<PassPort> result;
            for (uint32_t i = 0; i < count; ++i)
            {
                if (!values[i].name || values[i].same_extent_as_input < -1 || values[i].is_texture > 1)
                {
                    throw std::invalid_argument("Invalid native port");
                }
                result.push_back({values[i].name,
                                  values[i].is_texture ? PassResourceKind::eTexture : PassResourceKind::eBuffer,
                                  values[i].format,
                                  values[i].same_extent_as_input < 0 ?
                                      std::nullopt :
                                      std::optional<uint32_t>(values[i].same_extent_as_input)});
            }
            return result;
        };
        definition.inputs  = ports(input.inputs, input.input_count);
        definition.outputs = ports(input.outputs, input.output_count);
        for (uint32_t i = 0; i < input.parameter_count; ++i)
        {
            const auto& parameter = input.parameters[i];
            if (!parameter.name)
            {
                throw std::invalid_argument("Missing parameter name");
            }
            PassParameter copied {parameter.name, parameter.value, parameter.minimum, parameter.maximum};
            if (parameter.ui)
            {
                const auto& ui = *parameter.ui;
                if (ui.control < VULTRA_PASS_SLIDER || ui.control > VULTRA_PASS_READ_ONLY ||
                    (ui.choice_count && !ui.choices))
                {
                    throw std::invalid_argument("Invalid parameter presentation: " + copied.name);
                }
                copied.label       = ui.label ? ui.label : "";
                copied.description = ui.description ? ui.description : "";
                copied.control     = static_cast<PassControl>(ui.control);
                copied.shared      = ui.shared != 0;
                for (uint32_t choice = 0; choice < ui.choice_count; ++choice)
                {
                    if (!ui.choices[choice].label)
                    {
                        throw std::invalid_argument("Missing parameter choice label: " + copied.name);
                    }
                    copied.choices.push_back({ui.choices[choice].label, ui.choices[choice].value});
                }
            }
            definition.parameters.push_back(std::move(copied));
        }
        definition.displayName      = input.display_name ? input.display_name : "";
        definition.description      = input.description ? input.description : "";
        definition.requiredFeatures = input.required_features;
        VultraNativePassDefinition factory {};
        factory.output_count = input.output_count;
        factory.user_data    = input.user_data;
        factory.create       = input.create;
        definition.create    = [this, factory](Device&)
        {
            return std::make_unique<NativePass>(*this, factory);
        };
        m_Catalog.add(std::move(definition));
    }
} // namespace vultra
