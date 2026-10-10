#pragma once

#include <vultra/core/image/image.hpp>

namespace vultra
{
    struct FlipResult
    {
        Image                 error; // Scalar error replicated to RGB, alpha = 1; no display transfer.
        std::optional<double> mean;
        uint64_t              pixels          = 0;
        float                 pixelsPerDegree = 67;
    };

    // LDR-FLIP on explicitly tone-mapped linear RGB in [0,1]. The full frame defines filter context;
    // ROI/mask affect reduction only. No implicit exposure, transfer or HDR-FLIP exposure search.
    FlipResult evaluateFlip(const Image&           reference,
                            const Image&           test,
                            float                  pixelsPerDegree = 67,
                            MetricRegion           region          = {},
                            std::span<const float> mask            = {});

    // Mean RGB |(test-now - test-before) - (reference-now - reference-before)|.
    // Call only for consecutive frames of the same configuration; mask may be the intersection of two frames.
    std::optional<double> temporalError(const Image&           referenceBefore,
                                        const Image&           testBefore,
                                        const Image&           referenceNow,
                                        const Image&           testNow,
                                        MetricRegion           region = {},
                                        std::span<const float> mask   = {});
} // namespace vultra
