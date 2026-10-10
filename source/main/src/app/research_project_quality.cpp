#include "research_quality_work.hpp"

#include <vultra/core/base/logger.hpp>
#include <vultra/servers/rendering/texture_upload.hpp>

#include <flip/FLIP.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace vultra
{
    Image ResearchProjectApp::flipHeatmap(const Image& error)
    {
        validateImage(error);
        Image result {error.size, std::vector<float>(error.rgba.size())};
        for (size_t pixel = 0; pixel < result.rgba.size() / 4; ++pixel)
        {
            const auto& color =
                FLIP::MapMagma[uint32_t(std::round(std::clamp(error.rgba[pixel * 4], 0.0f, 1.0f) * 255))];
            result.rgba[pixel * 4]     = color.r;
            result.rgba[pixel * 4 + 1] = color.g;
            result.rgba[pixel * 4 + 2] = color.b;
            result.rgba[pixel * 4 + 3] = 1;
        }
        return result;
    }

    bool ResearchProjectApp::qualityBusy() const
    {
        return m_QualityWork && bool(m_QualityWork->job);
    }

    void ResearchProjectApp::stopQuality()
    {
        m_QualityWork.reset();
    }

    std::string ResearchProjectApp::qualitySignature()
    {
        auto state = nlohmann::json::parse(configuration().serialize());
        for (const auto* key : {"camera", "rigView", "trackingPose", "headset", "trackFrame", "view"})
        {
            state.erase(key);
        }
        for (const auto& identity : m_Research.shaderIdentities())
        {
            state["activeProjectSpirv"].push_back(identity.spirvHash);
        }
        return state.dump();
    }

    void ResearchProjectApp::recordQuality(VriCommandBuffer* cmd)
    {
        if (qualityBusy())
        {
            return;
        }
        if (!m_QualityWork)
        {
            m_QualityWork = std::make_unique<QualityWork>();
        }
        auto job             = std::make_unique<QualityWork::Job>();
        job->views           = m_Renderer->views();
        job->selections      = m_Renderer->selections();
        job->roi             = m_Roi;
        job->pixelsPerDegree = m_PixelsPerDegree;
        job->signature       = qualitySignature();
        job->consecutive     = m_QualityFrame != UINT64_MAX && job->views.index == m_QualityFrame + 1 &&
                           job->signature == m_QualityConfiguration;
        if (job->consecutive)
        {
            job->previousReference = m_PreviousReference;
            job->previousCurrent   = m_PreviousCurrent;
            job->previousMasks     = m_PreviousMasks;
        }
        std::vector<Texture*> sources;
        sources.reserve(10);
        job->copies.reserve(10);
        const auto copy = [&](Texture& texture)
        {
            auto staging = std::make_unique<ImageReadback>(m_Device, texture);
            job->copies.push_back(std::move(staging));
            sources.push_back(&texture);
        };
        for (uint32_t eye = 0; eye < 2; ++eye)
        {
            copy(m_Renderer->texture(StereoOutput::eLinearHdr, 0, eye));
            copy(m_Renderer->texture(StereoOutput::eLinearHdr, 1, eye));
            copy(m_Renderer->texture(StereoOutput::eLinearDisplay, 0, eye));
            copy(m_Renderer->texture(StereoOutput::eLinearDisplay, 1, eye));
            if (!m_Masks[eye].empty())
            {
                auto&       mask   = m_Renderer->resourceTexture(m_Masks[eye]);
                const auto& source = m_Renderer->texture(StereoOutput::eLinearHdr, 0, eye);
                if (mask.desc.width != source.desc.width || mask.desc.height != source.desc.height)
                {
                    throw std::invalid_argument("Quality mask extent differs from its eye");
                }
                job->hasMask[eye] = true;
                copy(mask);
            }
        }
        // Validate/allocate everything before recording: a rejected mask must not leave commands
        // referring to destroyed staging buffers in the render frame that continues after the error.
        for (size_t i = 0; i < sources.size(); ++i)
        {
            job->copies[i]->record(cmd, *sources[i]);
        }
        m_LastQualitySample = m_RenderedFrames;
        m_QualityRequested  = false;
        Logger::app().info("Queued quality snapshot frame {} (background CPU LDR-FLIP)", job->views.index);
        m_QualityWork->job = std::move(job);
    }

    void ResearchProjectApp::pollQuality(bool wait)
    {
        if (!qualityBusy())
        {
            return;
        }
        auto& work = *m_QualityWork;
        auto& job  = *work.job;
        if (!job.task)
        {
            // Called at the next update, after the normal single-frame submission completed.
            size_t copy = 0;
            for (size_t eye = 0; eye < 2; ++eye)
            {
                job.reference[eye]        = job.copies[copy++]->consume();
                job.current[eye]          = job.copies[copy++]->consume();
                job.displayReference[eye] = mapImage(job.copies[copy++]->consume(), {});
                job.displayCurrent[eye]   = mapImage(job.copies[copy++]->consume(), {});
                if (job.hasMask[eye])
                {
                    const auto mask = job.copies[copy++]->consume();
                    job.masks[eye].resize(size_t(mask.size.width) * mask.size.height);
                    for (size_t i = 0; i < job.masks[eye].size(); ++i)
                    {
                        job.masks[eye][i] = mask.rgba[i * 4] >= 0.5f ? 1.0f : 0.0f;
                    }
                }
            }
            job.copies.clear();
            job.task = std::make_unique<vtask::TaskSet>(
                1,
                1,
                [&job](vtask::Range)
                {
                    try
                    {
                        for (size_t eye = 0; eye < 2; ++eye)
                        {
                            job.whole[eye] = compare(job.reference[eye], job.current[eye]);
                            job.regions[eye] =
                                compareRegion(job.reference[eye], job.current[eye], job.roi, job.masks[eye]);
                            job.display[eye] = compareRegion(job.displayReference[eye],
                                                             job.displayCurrent[eye],
                                                             job.roi,
                                                             job.masks[eye]);
                            job.flip[eye]    = evaluateFlip(job.displayReference[eye],
                                                         job.displayCurrent[eye],
                                                         job.pixelsPerDegree,
                                                         job.roi,
                                                         job.masks[eye]);
                            if (job.consecutive)
                            {
                                auto intersection = job.masks[eye];
                                for (size_t i = 0; i < intersection.size(); ++i)
                                {
                                    intersection[i] *= job.previousMasks[eye][i];
                                }
                                job.temporal[eye] = temporalError(job.previousReference[eye],
                                                                  job.previousCurrent[eye],
                                                                  job.reference[eye],
                                                                  job.current[eye],
                                                                  job.roi,
                                                                  intersection);
                            }
                        }
                    }
                    catch (...)
                    {
                        job.error = std::current_exception();
                    }
                    job.done.store(true, std::memory_order_release);
                });
            work.scheduler.run(*job.task);
        }
        if (!wait && !job.done.load(std::memory_order_acquire))
        {
            return;
        }
        work.scheduler.wait(*job.task);
        if (job.error)
        {
            const auto error = job.error;
            work.job.reset();
            std::rethrow_exception(error);
        }
        const bool                              changed = job.signature != qualitySignature();
        std::array<std::unique_ptr<Texture>, 2> previews;
        for (size_t eye = 0; eye < 2; ++eye)
        {
            TextureAssetData asset;
            asset.format = TextureFormat::eRgba8Unorm;
            TextureSubresource level;
            const auto         mapped = flipHeatmap(job.flip[eye].error);
            level.size                = mapped.size;
            level.bytes.resize(mapped.rgba.size());
            for (size_t i = 0; i < level.bytes.size(); ++i)
            {
                level.bytes[i] = std::byte(uint8_t(std::round(mapped.rgba[i] * 255)));
            }
            asset.subresources.push_back(std::move(level));
            previews[eye] = uploadTextureAsset(m_Device, asset);
        }
        for (auto& preview : m_FlipPreviews)
        {
            if (preview)
            {
                m_Gui.forgetTexture(*preview);
            }
        }
        m_FlipPreviews         = std::move(previews);
        m_Metrics              = job.whole;
        m_DisplayMetrics       = std::move(job.display);
        m_RegionMetrics        = std::move(job.regions);
        m_Flip                 = std::move(job.flip);
        m_Temporal             = job.temporal;
        m_PreviousReference    = std::move(job.reference);
        m_PreviousCurrent      = std::move(job.current);
        m_PreviousMasks        = std::move(job.masks);
        m_QualityConfiguration = std::move(job.signature);
        m_QualityFrame         = job.views.index;
        m_MetricViews          = job.views;
        m_MetricSelections     = job.selections;
        m_HasQuality           = true;
        m_HasMetrics           = true;
        m_MetricsStale         = changed;
        Logger::app().info("Quality snapshot {} complete: FLIP left={} right={}",
                           m_QualityFrame,
                           m_Flip[0].mean ? std::to_string(*m_Flip[0].mean) : "N/A",
                           m_Flip[1].mean ? std::to_string(*m_Flip[1].mean) : "N/A");
        work.job.reset();
    }

    void ResearchProjectApp::measureQuality()
    {
        pollQuality(true);
        Frame frame(m_Device);
        recordQuality(frame.begin());
        frame.submitAndWait();
        pollQuality(true);
    }
} // namespace vultra
