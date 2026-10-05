#ifndef VULTRA_RESEARCH_HOST_H
#define VULTRA_RESEARCH_HOST_H

#include <vultra/api/vultra_experiment.generated.h>

#if defined(_WIN32)
#if defined(VULTRA_RESEARCH_BUILD)
#define VULTRA_RESEARCH_EXPORT __declspec(dllexport)
#else
#define VULTRA_RESEARCH_EXPORT __declspec(dllimport)
#endif
#else
#define VULTRA_RESEARCH_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C"
{
#endif
    /* Initialize to zero. Own one host; do not copy it. Context/table expire on destroy. */
    typedef struct VultraResearchHost
    {
        void*                      owner;
        void*                      context;
        const VultraExperimentApi* experiment;
    } VultraResearchHost;

    /* Bootstrap only. Experiment calls and language layouts are generated from annotated C++ methods. */
    VULTRA_RESEARCH_EXPORT VultraStatus vultra_research_create(uint32_t            version,
                                                               uint32_t            experimentApiSize,
                                                               uint64_t            features,
                                                               VultraResearchHost* host);
    VULTRA_RESEARCH_EXPORT void         vultra_research_destroy(VultraResearchHost* host);
#ifdef __cplusplus
}
#endif
#endif
