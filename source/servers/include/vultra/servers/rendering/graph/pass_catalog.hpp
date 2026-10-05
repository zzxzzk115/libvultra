#pragma once

#include <vultra/servers/rendering/graph/render_graph.hpp>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vultra
{
    enum class PassResourceKind
    {
        eTexture,
        eBuffer
    };

    struct PassPort
    {
        std::string             name;
        PassResourceKind        kind   = PassResourceKind::eTexture;
        VriFormat               format = VriFormat_Unknown;
        std::optional<uint32_t> sameExtentAsInput;
    };

    // Numeric parameters change command recording, not resource layout or graph topology.
    struct PassParameter
    {
        std::string name;
        double      defaultValue;
        double      minimum;
        double      maximum;
    };

    using PassParameters = std::map<std::string, double>;

    class GraphPass
    {
    public:
        virtual ~GraphPass() = default;
        // The parameter span stays valid with the owning BuiltPass. Read it when recording commands.
        virtual std::vector<RenderGraph::Resource> addPasses(RenderGraph&                           graph,
                                                             std::string_view                       name,
                                                             std::span<const RenderGraph::Resource> inputs,
                                                             std::span<const double>                parameters) = 0;
    };

    struct PassDefinition
    {
        std::string                                        type;
        std::vector<PassPort>                              inputs;
        std::vector<PassPort>                              outputs;
        std::vector<PassParameter>                         parameters;
        uint64_t                                           requiredFeatures = 0;
        std::function<std::unique_ptr<GraphPass>(Device&)> create;
    };

    // Own this state until the graph's final submission completes and its callbacks are discarded.
    struct BuiltPass
    {
        std::string name;
        std::string type;
        // Never resize after construction: recorded callbacks may retain a span into these values.
        std::vector<double>                parameterValues;
        std::unique_ptr<GraphPass>         instance;
        std::vector<RenderGraph::Resource> outputs;
    };

    // Explicitly owned by the application/context; no process-global registration.
    class PassCatalog
    {
    public:
        explicit PassCatalog(Device& device);
        void                  add(PassDefinition definition);
        const PassDefinition& definition(std::string_view type) const;

        std::span<const PassDefinition> definitions() const
        {
            return m_Definitions;
        }

        BuiltPass build(RenderGraph&                           graph,
                        std::string_view                       type,
                        std::string_view                       name,
                        std::span<const RenderGraph::Resource> inputs,
                        const PassParameters&                  parameters = {}) const;

        // After previous GPU use completes and before recording. Invalid values leave the pass unchanged.
        void setParameters(BuiltPass& pass, const PassParameters& parameters) const;

    private:
        Device&                     m_Device;
        std::vector<PassDefinition> m_Definitions;
    };
} // namespace vultra
