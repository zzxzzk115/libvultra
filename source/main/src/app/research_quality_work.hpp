#pragma once

#include <vultra/main/app/research_project_app.hpp>
#include <vultra/servers/rendering/research/capture.hpp>

#include <vtask/scheduler.hpp>
#include <vtask/task_set.hpp>

#include <atomic>
#include <exception>

namespace vultra
{
    struct ResearchProjectApp::QualityWork
    {
        struct Job
        {
            std::vector<std::unique_ptr<ImageReadback>> copies;
            std::array<Image, 2>                        reference;
            std::array<Image, 2>                        current;
            std::array<Image, 2>                        displayReference;
            std::array<Image, 2>                        displayCurrent;
            std::array<Image, 2>                        previousReference;
            std::array<Image, 2>                        previousCurrent;
            std::array<std::vector<float>, 2>           masks;
            std::array<std::vector<float>, 2>           previousMasks;
            std::array<bool, 2>                         hasMask {};
            std::array<ImageMetrics, 2>                 whole;
            std::array<RegionMetrics, 2>                display;
            std::array<RegionMetrics, 2>                regions;
            std::array<FlipResult, 2>                   flip;
            std::array<std::optional<double>, 2>        temporal;
            MetricRegion                                roi;
            float                                       pixelsPerDegree = 67;
            StereoFrameViews                            views;
            std::array<size_t, 2>                       selections;
            std::string                                 signature;
            bool                                        consecutive = false;
            std::atomic_bool                            done {false};
            std::exception_ptr                          error;
            std::unique_ptr<vtask::TaskSet>             task;
        };

        // One bounded CPU job, one background worker; no engine/GPU mutation on that worker.
        vtask::Scheduler     scheduler {2};
        std::unique_ptr<Job> job;

        ~QualityWork()
        {
            scheduler.waitAll();
        }
    };
} // namespace vultra
