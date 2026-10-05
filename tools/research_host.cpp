#include "../examples/research/color_gain.hpp"

#include <vultra/api/experiment_bridge.hpp>
#include <vultra/api/research_host.h>
#include <vultra/core/base/logger.hpp>
#include <vultra/main/packaged_resources.hpp>
#include <vultra/platform/os/file.hpp>
#include <vultra/scripting/experiment_host.hpp>

#include <memory>

namespace
{
    constexpr unsigned char kGainShader[] = {
#include "color_gain.slang.h"
    };

    struct ResearchHost
    {
        explicit ResearchHost(uint64_t features) :
            device(true, nullptr, features),
            catalog(device),
            experiments(device,
                        catalog,
                        resources.engineRoot(),
                        {.cacheDirectory = resources.engineRoot().parent_path() / "cache"})
        {
            const auto shader = resources.engineRoot() / "examples/research/shaders/color_gain.slang";
            std::filesystem::create_directories(shader.parent_path());
            static_assert(kGainShader[std::size(kGainShader) - 1] == 0);
            vultra::writeFileAtomically(shader,
                                        std::as_bytes(std::span(kGainShader).first(std::size(kGainShader) - 1)));
            catalog.add(research::colorGainDefinition());
        }

        // Stop sessions/scripts and finish GPU work before deleting embedded shader files.
        vultra::PackagedResources resources;
        vultra::Device            device;
        vultra::PassCatalog       catalog;
        vultra::ExperimentHost    experiments;
    };
} // namespace

// NOLINTNEXTLINE(readability-identifier-naming): Public bootstrap C symbol.
extern "C" VULTRA_RESEARCH_EXPORT VultraStatus vultra_research_create(uint32_t            version,
                                                                      uint32_t            experimentApiSize,
                                                                      uint64_t            features,
                                                                      VultraResearchHost* host)
{
    if (!host || host->owner || version != VULTRA_ABI_VERSION || experimentApiSize != sizeof(VultraExperimentApi) ||
        (features != 0 && features != (VriFeature_RayQuery | VriFeature_Bindless)))
    {
        return VULTRA_STATUS_INVALID_ARGUMENT;
    }
    try
    {
        auto owner       = std::make_unique<ResearchHost>(features);
        host->context    = &owner->experiments;
        host->experiment = &vultra::experimentApi();
        host->owner      = owner.release();
        return VULTRA_STATUS_OK;
    }
    catch (const std::exception& error)
    {
        vultra::Logger::app().error("Create research host: {}", error.what());
        return VULTRA_STATUS_ERROR;
    }
    catch (...)
    {
        vultra::Logger::app().error("Create research host: unknown exception");
        return VULTRA_STATUS_ERROR;
    }
}

// NOLINTNEXTLINE(readability-identifier-naming): Public bootstrap C symbol.
extern "C" VULTRA_RESEARCH_EXPORT void vultra_research_destroy(VultraResearchHost* host)
{
    if (host)
    {
        auto* owner = static_cast<ResearchHost*>(host->owner);
        *host       = {};
        delete owner;
    }
}
