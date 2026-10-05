#include <vultra/servers/rendering/research/graph_report.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <set>

namespace vultra
{
    std::string graphReport(const RenderGraph&            graph,
                            std::span<const PassTiming>   timings,
                            std::span<const GraphCapture> images,
                            std::string_view              gpuCapture)
    {
        using Json               = nlohmann::json;
        const auto snapshot      = graph.snapshot();
        const bool hasGpuTimings = graph.device().core.GetDeviceDesc(graph.device().handle)->hasTimestampQueries;
        Json       report {{"format", "vultra.graph.report"},
                           {"version", 1},
                           {"history_epoch", graph.historyEpoch()},
                           {"gpu_capture", gpuCapture.empty() ? Json(nullptr) : Json(gpuCapture)},
                           {"resources", Json::array()},
                           {"passes", Json::array()},
                           {"events", Json::array()},
                           {"images", Json::array()}};
        std::set<uint32_t> allocations;
        uint64_t           physicalBytes = 0;
        bool               memoryKnown   = true;
        for (size_t i = 0; i < snapshot.resources.size(); ++i)
        {
            const auto& resource = snapshot.resources[i];
            report["resources"].push_back(
                {{"id", i},
                 {"name", resource.name},
                 {"active", resource.active},
                 {"kind", resource.isTexture ? "texture" : "buffer"},
                 {"imported", resource.imported},
                 {"exported", resource.exported},
                 {"history", resource.history},
                 {"allocation", resource.allocation == UINT32_MAX ? Json(nullptr) : Json(resource.allocation)},
                 {"first_use", resource.firstUse == UINT32_MAX ? Json(nullptr) : Json(resource.firstUse)},
                 {"last_use", resource.lastUse == UINT32_MAX ? Json(nullptr) : Json(resource.lastUse)},
                 {"memory_bytes", resource.memoryKnown ? Json(resource.memoryBytes) : Json(nullptr)}});
            if (resource.active && !resource.imported && allocations.insert(resource.allocation).second)
            {
                memoryKnown = memoryKnown && resource.memoryKnown;
                physicalBytes += resource.memoryBytes;
            }
        }
        report["graph_owned_bytes"] = memoryKnown ? Json(physicalBytes) : Json(nullptr);
        for (size_t i = 0; i < snapshot.passes.size(); ++i)
        {
            const auto& pass = snapshot.passes[i];
            Json        uses = Json::array();
            for (const auto& use : pass.uses)
            {
                uses.push_back({{"resource", use.resourceIndex}, {"usage", usageName(use.usage)}});
            }
            report["passes"].push_back({{"id", i},
                                        {"name", pass.name},
                                        {"active", pass.active},
                                        {"dependencies", pass.dependencies},
                                        {"uses", uses}});
        }
        for (size_t i = 0; i < timings.size(); ++i)
        {
            const auto& event = timings[i];
            const auto  pass  = std::ranges::find(snapshot.passes, event.name, &RenderGraph::PassInfo::name);
            report["events"].push_back(
                {{"id", i},
                 {"name", event.name},
                 {"depth", event.depth},
                 {"parent", event.parent == UINT32_MAX ? Json(nullptr) : Json(event.parent)},
                 {"pass", pass == snapshot.passes.end() ? Json(nullptr) : Json(pass - snapshot.passes.begin())},
                 {"cpu_ms", event.cpuMs},
                 {"gpu_ms", hasGpuTimings ? Json(event.gpuMs) : Json(nullptr)},
                 {"cpu_barrier_ms", event.cpuBarrierMs},
                 {"gpu_barrier_ms", hasGpuTimings ? Json(event.gpuBarrierMs) : Json(nullptr)}});
        }
        for (const auto& image : images)
        {
            if (image.resource.graph != &graph || image.resource.index >= snapshot.resources.size())
            {
                throw std::invalid_argument("Graph capture belongs to another graph");
            }
            Json producers = Json::array();
            for (size_t i = 0; i < snapshot.passes.size(); ++i)
            {
                const auto& pass = snapshot.passes[i];
                for (const auto& use : pass.uses)
                {
                    if (pass.active && use.resourceIndex == image.resource.index &&
                        (use.usage == Usage::eColorWrite || use.usage == Usage::eColorReadWrite ||
                         use.usage == Usage::eDepthWrite || use.usage == Usage::eDepthReadWrite ||
                         use.usage == Usage::eStorageWrite || use.usage == Usage::eStorageReadWrite ||
                         use.usage == Usage::eCopyDestination))
                    {
                        producers.push_back(i);
                        break;
                    }
                }
            }
            report["images"].push_back({{"port", image.port},
                                        {"file", image.file},
                                        {"resource", image.resource.index},
                                        {"producers", producers}});
        }
        return report.dump(2) + '\n';
    }
} // namespace vultra
