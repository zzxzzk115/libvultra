#pragma once

#include <vultra/platform/os/file.hpp>
#include <vultra/servers/rendering/graph/graph_definition.hpp>

#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

namespace vultra::test
{
    inline std::expected<void, std::string> graphPersistenceExpectedProbe(const std::filesystem::path& directory)
    {
        std::error_code error;
        if (!std::filesystem::create_directory(directory, error) || error)
        {
            return std::unexpected("Create fresh graph persistence directory: " + error.message());
        }
        GraphDefinition graph {{{"shade_\xe7\x81\xaf", "unregistered.type", {{"gain", 0.25}, {"offset", -2}}}},
                               {{"scene.hdr", "shade_\xe7\x81\xaf.source"}},
                               {"shade_\xe7\x81\xaf.color"}};
        const auto      file = directory / "graph.vgraph";
        auto            text = graph.serializeExpected();
        if (!text || !graph.saveExpected(file))
        {
            return std::unexpected("Serialize/save valid editable graph failed");
        }
        auto loaded = GraphDefinition::loadExpected(file);
        auto parsed = GraphDefinition::parseExpected(*text);
        if (!loaded || !parsed || loaded->serializeExpected() != text || parsed->serializeExpected() != text ||
            text->find("\"version\": 1") == std::string::npos)
        {
            return std::unexpected("Version-1 graph persistence changed valid UTF-8, numeric values or ordering");
        }
        auto invalidPath = file.native();
        invalidPath.push_back(std::filesystem::path::value_type {});
        invalidPath += std::filesystem::path("ignored").native();
        if (graph.saveExpected({}) || GraphDefinition::loadExpected({}) || graph.saveExpected(invalidPath) ||
            GraphDefinition::loadExpected(invalidPath))
        {
            return std::unexpected("Graph persistence accepted an empty or truncated native path");
        }
        const std::array nonfinite {std::numeric_limits<double>::quiet_NaN(),
                                    std::numeric_limits<double>::infinity(),
                                    -std::numeric_limits<double>::infinity()};
        for (const auto value : nonfinite)
        {
            auto invalid                         = graph;
            invalid.passes[0].parameters["gain"] = value;
            const auto serialized                = invalid.serializeExpected();
            const auto saved                     = invalid.saveExpected(file);
            if (serialized || saved || serialized.error().find("finite") == std::string::npos ||
                saved.error().find("finite") == std::string::npos)
            {
                return std::unexpected("Graph persistence accepted a nonfinite parameter or hid its cause");
            }
            auto previous = GraphDefinition::loadExpected(file);
            if (!previous || previous->serializeExpected() != text)
            {
                return std::unexpected("Invalid graph save replaced the preceding valid file");
            }
        }
        const std::string invalidUtf8("\xc0\xaf", 2);
        for (unsigned field = 0; field < 6; ++field)
        {
            auto invalid = graph;
            switch (field)
            {
                case 0:
                    invalid.passes[0].id = invalidUtf8;
                    break;
                case 1:
                    invalid.passes[0].type = invalidUtf8;
                    break;
                case 2:
                    invalid.passes[0].parameters.emplace(invalidUtf8, 1);
                    break;
                case 3:
                    invalid.edges[0].from = invalidUtf8;
                    break;
                case 4:
                    invalid.edges[0].to = invalidUtf8;
                    break;
                case 5:
                    invalid.outputs[0] = invalidUtf8;
                    break;
            }
            if (invalid.serializeExpected() || invalid.saveExpected(file))
            {
                return std::unexpected("Graph persistence silently changed invalid UTF-8 identities");
            }
            auto previous = GraphDefinition::loadExpected(file);
            if (!previous || previous->serializeExpected() != text)
            {
                return std::unexpected("Invalid UTF-8 save changed the previous graph");
            }
        }
        const std::array malformed {
            std::string_view("{"),
            std::string_view("[]"),
            std::string_view(R"({"format":"vultra.graph","version":2,"passes":[],"edges":[],"outputs":[]})"),
            std::string_view(R"({"format":"vultra.graph","version":1,"passes":{},"edges":[],"outputs":[]})"),
            std::string_view(R"({"format":"vultra.graph","version":1,"passes":[{}],"edges":[],"outputs":[]})"),
            std::string_view(
                R"({"format":"vultra.graph","version":1,"passes":[{"id":"x","type":"x","parameters":{"gain":true}}],"edges":[],"outputs":[]})"),
            std::string_view(
                R"({"format":"vultra.graph","version":1,"passes":[{"id":"x","type":"x","parameters":{"gain":1e400}}],"edges":[],"outputs":[]})"),
            std::string_view(
                R"({"format":"vultra.graph","version":1,"passes":[],"edges":[{"from":4,"to":"x.y"}],"outputs":[]})"),
            std::string_view(R"({"format":"vultra.graph","version":1,"passes":[],"edges":[],"outputs":[false]})")};
        for (const auto source : malformed)
        {
            if (GraphDefinition::parseExpected(source))
            {
                return std::unexpected("Malformed graph input escaped the checked parser");
            }
        }
        const auto broken = directory / "broken.vgraph";
        if (!writeFileAtomicallyExpected(broken, std::as_bytes(std::span(malformed[0]))) ||
            GraphDefinition::loadExpected(broken) || GraphDefinition::loadExpected(directory / "missing.vgraph") ||
            GraphDefinition::loadExpected(directory))
        {
            return std::unexpected("Graph file parse/open/read failure was not reported");
        }
        const auto blocked = directory / "blocked.vgraph";
        if (!std::filesystem::create_directory(blocked, error) || error)
        {
            return std::unexpected("Create blocked graph publication target failed");
        }
        const std::string_view sentinel = "retain this directory entry";
        if (!writeFileAtomicallyExpected(blocked / "sentinel", std::as_bytes(std::span(sentinel))) ||
            graph.saveExpected(blocked) || graph.saveExpected(directory / "missing" / "graph.vgraph"))
        {
            return std::unexpected("Rejected graph publication did not report its filesystem failure");
        }
        std::filesystem::directory_iterator entries(directory, error);
        if (error)
        {
            return std::unexpected("Inspect failed graph publication cleanup: " + error.message());
        }
        for (const auto& entry : entries)
        {
            if (entry.path().extension() == ".tmp")
            {
                return std::unexpected("Failed graph publication left a temporary file");
            }
        }
        graph.passes[0].parameters["gain"] = 2;
        auto replacement                   = graph.serializeExpected();
        if (!replacement || replacement == text || !graph.saveExpected(file))
        {
            return std::unexpected("Graph save did not recover after rejected candidates");
        }
        auto recovered = GraphDefinition::loadExpected(file);
        if (!recovered || recovered->serializeExpected() != replacement)
        {
            return std::unexpected("Successful graph replacement did not publish its complete contents");
        }
        constexpr size_t limit = 64 * 1024 * 1024;
        if (GraphDefinition::parseExpected(std::string(limit + 1, ' ')))
        {
            return std::unexpected("Unbounded graph text was accepted");
        }
        std::filesystem::resize_file(broken, limit + 1, error);
        if (error || GraphDefinition::loadExpected(broken))
        {
            return std::unexpected("Oversized graph file did not return an Expected error");
        }
        std::filesystem::resize_file(broken, 0, error);
        if (error)
        {
            return std::unexpected("Release oversized graph fixture storage: " + error.message());
        }
        std::puts("GRAPH_PERSISTENCE_EXPECTED roundtrip=byte_stable invalid_utf8=6 nonfinite=3 malformed=9 "
                  "atomic=preserved retry=passed bounded=64MiB");
        return {};
    }
} // namespace vultra::test
