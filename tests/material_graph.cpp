#include <vultra/assets/material_graph.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <bit>
#include <cstdio>
#include <limits>

namespace
{
    using namespace vultra;
    using Result = std::expected<void, std::string>;

    MaterialGraph fixture()
    {
        const auto parameter = [](std::string name)
        {
            return MaterialGraphValue {MaterialGraphSource::eParameter, std::move(name)};
        };
        const auto node = [](std::string name)
        {
            return MaterialGraphValue {MaterialGraphSource::eNode, std::move(name)};
        };
        MaterialGraphValue half;
        half.source  = MaterialGraphSource::eLiteral;
        half.literal = 0.5f;
        MaterialGraph graph;
        graph.entry      = "materialPattern";
        graph.resultType = "float4";
        graph.parameters = {{"tint", "float4"},
                            {"gain", "float"},
                            {"uv", "float2"},
                            {"image", "Texture2D<float4>"},
                            {"nearest", "SamplerState"}};
        graph.code       = "float4 sampleColor(Texture2D<float4> image, SamplerState nearest, float2 uv)\n"
                           "{ return image.SampleLevel(nearest, uv, 0); }\n"
                           "float4 scaleColor(float4 sample, float4 tint, float gain)\n"
                           "{ return float4(sample.rgb * tint.rgb * gain, sample.a * tint.a); }\n";
        graph.nodes      = {{"shade", "scaleColor", {node("sample"), parameter("tint"), node("adjusted")}},
                            {"orphan", "notRequiredByResult", {}},
                            {"sample", "sampleColor", {parameter("image"), parameter("nearest"), parameter("uv")}},
                            {"adjusted", "mul", {parameter("gain"), half}}};
        graph.result     = node("shade");
        return graph;
    }

    Result run()
    {
        auto graph  = fixture();
        auto source = graph.sourceExpected("test.vmatgraph");
        auto text   = graph.serializeExpected();
        if (!source || !text)
        {
            return std::unexpected(source ? text.error() : source.error());
        }
        if (source->find("vultraGraph_orphan") != std::string::npos ||
            source->find("vultraGraph_sample") > source->find("let vultraGraph_shade") ||
            source->find("vultraGraph_adjusted") > source->find("let vultraGraph_shade") ||
            source->find("test.vmatgraph:shade") == std::string::npos)
        {
            return std::unexpected("Graph source lost dependency order/pruning/node identity");
        }
        auto parsed = MaterialGraph::parseExpected(*text);
        if (!parsed || parsed->serializeExpected() != text || parsed->sourceExpected("test.vmatgraph") != source)
        {
            return std::unexpected("Graph authoring state did not round trip exactly");
        }
        std::ranges::reverse(graph.nodes);
        if (graph.serializeExpected() != text || graph.sourceExpected("test.vmatgraph") != source)
        {
            return std::unexpected("Graph source/persistence depends on node storage order");
        }
        for (int edit = 0; edit < 15; ++edit)
        {
            auto bad = fixture();
            switch (edit)
            {
                case 0:
                    bad.nodes.push_back(bad.nodes.front());
                    break;
                case 1:
                    bad.nodes[0].arguments[0].name = "missing";
                    break;
                case 2:
                    bad.nodes[0].arguments[1].name = "missing";
                    break;
                case 3:
                    bad.nodes[2].arguments[0] = bad.result;
                    break;
                case 4:
                    bad.nodes[1].arguments.push_back({MaterialGraphSource::eNode, "orphan"});
                    break;
                case 5:
                    bad.parameters.push_back(bad.parameters.front());
                    break;
                case 6:
                    bad.parameters.front().name = "vultraGraph_shade";
                    break;
                case 7:
                    bad.nodes.front().function = "f(); invalid()";
                    break;
                case 8:
                    bad.nodes.front().arguments[0].selection = "rgb; return other";
                    break;
                case 9:
                    bad.nodes.front().arguments[0].selection = "[4294967296]";
                    break;
                case 10:
                    bad.result.source = MaterialGraphSource(99);
                    break;
                case 11:
                    bad.entry = "invalid-name";
                    break;
                case 12:
                    bad.resultType = "float4;";
                    break;
                case 13:
                    bad.code.push_back(char(0));
                    break;
                case 14:
                    bad.result         = {};
                    bad.result.source  = MaterialGraphSource::eLiteral;
                    bad.result.literal = std::numeric_limits<float>::infinity();
                    break;
            }
            if (bad.sourceExpected() || bad.serializeExpected())
            {
                return std::unexpected("Invalid graph emitted, case " + std::to_string(edit));
            }
        }
        const auto document = nlohmann::json::parse(*text, nullptr, false);
        for (int edit = 0; edit < 15; ++edit)
        {
            auto bad = document;
            switch (edit)
            {
                case 0:
                    bad["version"] = 2;
                    break;
                case 1:
                    bad["version"] = 1.0;
                    break;
                case 2:
                    bad["kind"] = "other";
                    break;
                case 3:
                    bad.erase("result");
                    break;
                case 4:
                    bad["extra"] = true;
                    break;
                case 5:
                    bad["nodes"] = 3;
                    break;
                case 6:
                    bad["parameters"][0]["name"] = false;
                    break;
                case 7:
                    bad["nodes"][0]["arguments"] = false;
                    break;
                case 8:
                    bad["result"]["source"] = "unknown";
                    break;
                case 9:
                    bad["result"]["name"] = "missing";
                    break;
                case 10:
                    bad["nodes"][0]["arguments"][1]["type"] = "other";
                    break;
                case 11:
                    bad["nodes"][0]["arguments"][1]["value"] = "not numeric";
                    break;
                case 12:
                    bad["nodes"][0]["arguments"][1]["type"]  = "uint";
                    bad["nodes"][0]["arguments"][1]["value"] = -1;
                    break;
                case 13:
                    bad["nodes"][0]["arguments"][1]["type"]  = "int";
                    bad["nodes"][0]["arguments"][1]["value"] = uint64_t(UINT32_MAX);
                    break;
                case 14:
                    bad["parameters"][0]["type"] = "float4\ninvalid";
                    break;
            }
            if (MaterialGraph::parseExpected(bad.dump()))
            {
                return std::unexpected("Malformed graph accepted, case " + std::to_string(edit));
            }
        }
        for (const auto value : {std::variant<float, int32_t, uint32_t, bool>(-0.0f),
                                 std::variant<float, int32_t, uint32_t, bool>(INT32_MIN),
                                 std::variant<float, int32_t, uint32_t, bool>(UINT32_MAX),
                                 std::variant<float, int32_t, uint32_t, bool>(true)})
        {
            auto scalar           = fixture();
            scalar.result         = {};
            scalar.result.source  = MaterialGraphSource::eLiteral;
            scalar.result.literal = value;
            auto saved            = scalar.serializeExpected();
            if (!saved)
            {
                return std::unexpected(saved.error());
            }
            auto restored = MaterialGraph::parseExpected(*saved);
            if (!restored || restored->result.literal != value || restored->serializeExpected() != saved)
            {
                return std::unexpected("Typed graph literal changed during persistence");
            }
            if (std::holds_alternative<float>(value) &&
                std::bit_cast<uint32_t>(std::get<float>(restored->result.literal)) != 0x80000000u)
            {
                return std::unexpected("Negative zero graph literal lost its sign");
            }
        }
        auto invalidUtf8 = fixture();
        invalidUtf8.code.push_back(char(0xff));
        if (invalidUtf8.serializeExpected() || invalidUtf8.sourceExpected())
        {
            return std::unexpected("Graph source accepted invalid UTF-8");
        }
        auto duplicate = *text;
        duplicate.insert(1, "\"entry\":\"overridden\",");
        if (MaterialGraph::parseExpected(duplicate))
        {
            return std::unexpected("Graph document accepted a duplicate object key");
        }
        return {};
    }
} // namespace

int main()
{
    if (auto checked = run(); !checked)
    {
        std::fprintf(stderr, "%s\n", checked.error().c_str());
        return 1;
    }
    std::puts("MATERIAL_GRAPH_CPU structural_reject=15 malformed_reject=15 typed_literals=4 deterministic=1 pruning=1");
    return 0;
}
