#pragma once

#include "builtin_owner_expected_probe.hpp"
#include "scene_position_write.hpp"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <numeric>

namespace vultra::test
{
    inline std::expected<void, std::string> builtinFrameExpectedProbe(Device&                      device,
                                                                      GpuScene&                    scene,
                                                                      Environment&                 environment,
                                                                      const AssetSource*           shaders   = nullptr,
                                                                      std::span<const SceneVertex> positions = {})
    {
        auto created = BuiltinRenderer::create(device, scene, environment, VriFormat_RGBA8_UNORM, shaders);
        if (!created)
        {
            return std::unexpected(created.error());
        }
        auto& renderer                     = **created;
        renderer.settings.shadowResolution = 32;
        renderer.settings.shadowFilter     = ShadowFilter::eHard;
        renderer.settings.cacheShadows     = true;
        renderer.settings.ibl              = false;
        renderer.settings.skybox           = false;
        constexpr Extent size {17, 13};
        RenderCamera     camera;
        camera.view       = glm::lookAtRH(glm::vec3(0, 0, 3), glm::vec3(0), glm::vec3(0, 1, 0));
        camera.projection = glm::perspectiveRH_ZO(glm::radians(60.0f),
                                                  float(size.width) / size.height,
                                                  camera.nearPlane,
                                                  camera.farPlane);
        auto moved        = camera;
        moved.view        = glm::lookAtRH(glm::vec3(1, 0.2f, 3), glm::vec3(0), glm::vec3(0, 1, 0));
        RenderGraph graph(device);
        auto        outputs = renderer.addPassesExpected(graph, size);
        if (!outputs)
        {
            return std::unexpected(outputs.error());
        }
        bool             rejectTail = false;
        const std::array tailUses {RenderGraph::Use {outputs->hdr, Usage::eSampled}};
        auto             tail = graph.addPassExpected(
            "After built-in scene",
            tailUses,
            [&](auto*, auto&) -> std::expected<void, std::string>
            {
                if (rejectTail)
                {
                    return std::unexpected("injected post-scene recording failure");
                }
                return {};
            },
            true);
        if (!tail)
        {
            return std::unexpected(tail.error());
        }
        const std::array resources {outputs->hdr,
                                    outputs->color,
                                    outputs->shadows[0],
                                    outputs->shadows[1],
                                    outputs->shadows[2],
                                    outputs->shadows[3]};
        for (const auto resource : resources)
        {
            if (auto exported = graph.exportResourceExpected(resource); !exported)
            {
                return std::unexpected(exported.error());
            }
        }
        if (auto compiled = graph.compileExpected(); !compiled)
        {
            return std::unexpected(compiled.error());
        }
        auto frame = Frame::create(device);
        if (!frame)
        {
            return std::unexpected(frame.error());
        }
        using Pixels       = std::array<std::vector<float>, 6>;
        const auto capture = [&]() -> std::expected<Pixels, std::string>
        {
            Pixels result;
            for (size_t i = 0; i < resources.size(); ++i)
            {
                auto texture = graph.getTextureExpected(resources[i]);
                if (!texture)
                {
                    return std::unexpected(texture.error());
                }
                auto image = readbackExpected(device, **texture);
                if (!image)
                {
                    return std::unexpected(image.error());
                }
                result[i] = std::move(image->rgba);
            }
            return result;
        };
        const auto shadowCount = [&]
        {
            const auto counts = renderer.primitiveCounts();
            return std::accumulate(counts.begin() + 1, counts.end(), 0u);
        };
        const auto render = [&](const RenderCamera& view) -> std::expected<Pixels, std::string>
        {
            if (auto prepared = renderer.prepareExpected(view, graph, *outputs); !prepared)
            {
                return std::unexpected(prepared.error());
            }
            auto commands = (*frame)->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            if (auto recorded = graph.executeExpected(*commands); !recorded)
            {
                return std::unexpected(recorded.error());
            }
            if (auto completed = (*frame)->submitAndWaitExpected(); !completed)
            {
                return std::unexpected(completed.error());
            }
            renderer.completeFrame();
            return capture();
        };
        auto accepted = render(camera);
        if (!accepted)
        {
            return std::unexpected(accepted.error());
        }
        if (!shadowCount() || std::ranges::none_of((*accepted)[0],
                                                   [](float value)
                                                   {
                                                       return value > 0 && value < 1;
                                                   }))
        {
            return std::unexpected("Built-in failure fixture did not render geometry and cold shadow casters");
        }
        auto warm = render(camera);
        if (!warm || *warm != *accepted || shadowCount())
        {
            return std::unexpected("Warm built-in shadows were not reused exactly");
        }

        if (!positions.empty())
        {
            auto narrowed = std::vector<SceneVertex>(positions.begin(), positions.end());
            for (auto& vertex : narrowed)
            {
                vertex.position.x *= .25f;
            }
            // Both versions fit these same conservative bounds: only the geometry revision invalidates shadows.
            const std::array bounds {PrimitiveBounds {{0, 0, 0}, {1, 1, 0}}};
            if (auto changed = writeScenePositionsExpected(device, scene, narrowed, bounds); !changed)
            {
                return changed;
            }
            auto edited = render(camera);
            if (!edited || !shadowCount() || (*edited)[0] == (*accepted)[0])
            {
                return std::unexpected("Position edit retained stale raster pixels or cached shadow casters");
            }
            auto stable = render(camera);
            if (!stable || *stable != *edited || shadowCount())
            {
                return std::unexpected("Unchanged position revision redrew shadows or changed raster pixels");
            }
            if (auto changed = writeScenePositionsExpected(device, scene, positions, bounds); !changed)
            {
                return changed;
            }
            auto restored = render(camera);
            if (!restored || *restored != *accepted || !shadowCount())
            {
                return std::unexpected("Restored vertex positions did not restore complete raster/shadow images");
            }
            std::puts(
                "SCENE_POSITION_RASTER full_images=6 restored=exact shared_graph=retained shadow_cache=invalidated");
        }

        // Appending malformed candidates must preserve this compiled graph, callbacks and native allocations.
        const auto before      = graph.snapshot();
        auto       invalidPath = renderer.settings.path;
        renderer.settings.path = RenderPath::eReferencePathTracing;
        auto rejectedPath      = renderer.addScenePassesExpected(graph, size);
        renderer.settings.path = invalidPath;
        if (rejectedPath || renderer.addScenePassesExpected(graph, {}) ||
            renderer.addGBufferPassesExpected(graph, outputs->hdr, size) ||
            renderer.addDeferredLightingPassExpected(graph, outputs->depth, outputs->gbuffer, outputs->shadows) ||
            renderer.addToneMappingPassExpected(graph, outputs->hdr, {size.width + 1, size.height}) ||
            graph.snapshot().resources.size() != before.resources.size() ||
            graph.snapshot().passes.size() != before.passes.size())
        {
            return std::unexpected("Rejected built-in append changed the compiled graph");
        }
        auto unchanged = render(camera);
        if (!unchanged || *unchanged != *accepted || shadowCount())
        {
            return std::unexpected("Rejected built-in append damaged the accepted GPU graph/cache");
        }

        for (uint32_t failure = 0; failure < 3; ++failure)
        {
            auto altered = moved;
            if (failure == 0)
            {
                altered.projection[0][0] = std::numeric_limits<float>::quiet_NaN();
            }
            const auto core = device.core;
            if (failure == 1)
            {
                device.core.MapBuffer = [](VriBuffer*, uint64_t, uint64_t) -> void*
                {
                    return nullptr;
                };
            }
            auto rejected = renderer.prepareExpected(altered, graph, *outputs);
            device.core   = core;
            if (failure == 2)
            {
                // A fresh graph has uncached native shadow views, while the accepted graph stays alive.
                RenderGraph candidate(device);
                auto        candidateOutputs = renderer.addPassesExpected(candidate, size);
                if (!candidateOutputs)
                {
                    return std::unexpected(candidateOutputs.error());
                }
                if (auto exported = candidate.exportResourceExpected(candidateOutputs->color); !exported)
                {
                    return std::unexpected(exported.error());
                }
                if (auto compiled = candidate.compileExpected(); !compiled)
                {
                    return std::unexpected(compiled.error());
                }
                builtin_owner_probe::NativeFault native(device);
                native.arm(builtin_owner_probe::Failure::eTextureView);
                rejected = renderer.prepareExpected(moved, candidate, *candidateOutputs);
                if (!native.injected)
                {
                    return std::unexpected("Built-in prepare view rejection did not reach native creation");
                }
            }
            if (rejected)
            {
                return std::unexpected("Built-in invalid camera/map/view preparation was accepted");
            }
            auto commands = (*frame)->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            auto forbidden = graph.executeExpected(*commands);
            if (auto discarded = (*frame)->discardExpected(); !discarded)
            {
                return std::unexpected(discarded.error());
            }
            if (forbidden || forbidden.error().find("Prepare the built-in") == std::string::npos)
            {
                return std::unexpected("Failed preparation allowed built-in scene recording");
            }
            auto retained = capture();
            if (!retained || *retained != *accepted)
            {
                return std::unexpected("Rejected preparation changed accepted GPU pixels");
            }
            auto recovered = render(camera);
            if (!recovered || *recovered != *accepted || shadowCount())
            {
                return std::unexpected("Rejected preparation damaged accepted shadow-cache reuse");
            }
        }

        // Recording rollback uses the registered per-cascade CPU state without a caller checkpoint.
        if (auto prepared = renderer.prepareExpected(moved, graph, *outputs); !prepared)
        {
            return std::unexpected(prepared.error());
        }
        rejectTail    = true;
        auto commands = (*frame)->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        auto rejectedRecording = graph.executeExpected(*commands);
        rejectTail             = false;
        if (auto discarded = (*frame)->discardExpected(); !discarded)
        {
            return std::unexpected(discarded.error());
        }
        if (rejectedRecording || rejectedRecording.error().find("post-scene") == std::string::npos)
        {
            return std::unexpected("Built-in post-scene recording failure was lost");
        }
        auto recovered = render(camera);
        if (!recovered || *recovered != *accepted || shadowCount())
        {
            return std::unexpected("Recording rollback lost the accepted shadow cache");
        }

        for (uint32_t failure = 0; failure < 3; ++failure)
        {
            if (auto prepared = renderer.prepareExpected(moved, graph, *outputs); !prepared)
            {
                return std::unexpected(prepared.error());
            }
            auto checkpoint = graph.checkpointExpected();
            commands        = (*frame)->beginExpected();
            if (!checkpoint || !commands)
            {
                return std::unexpected("Prepare abandoned built-in frame failed");
            }
            if (auto recorded = graph.executeExpected(*commands); !recorded)
            {
                return std::unexpected(recorded.error());
            }
            if (!shadowCount())
            {
                return std::unexpected("Abandoned built-in frame did not change shadow contents");
            }
            if (failure != 0)
            {
                const auto core = device.core;
                if (failure == 1)
                {
                    device.core.EndCommandBuffer = [](VriCommandBuffer*)
                    {
                        return VriResult_Failure;
                    };
                }
                else
                {
                    device.core.QueueSubmit = [](VriQueue*, const VriQueueSubmitDesc*)
                    {
                        return VriResult_Failure;
                    };
                }
                auto submitted = (*frame)->submitExpected();
                device.core    = core;
                if (submitted)
                {
                    return std::unexpected("Injected built-in submission rejection was accepted");
                }
            }
            if (auto discarded = (*frame)->discardExpected(); !discarded)
            {
                return std::unexpected(discarded.error());
            }
            if (auto restored = graph.restoreUnsubmittedExpected(std::move(*checkpoint)); !restored)
            {
                return std::unexpected(restored.error());
            }
            auto retained = capture();
            if (!retained || *retained != *accepted)
            {
                return std::unexpected("Abandoned built-in frame changed accepted pixels");
            }
            recovered = render(camera);
            if (!recovered || *recovered != *accepted || shadowCount())
            {
                return std::unexpected("Unsubmitted checkpoint lost the accepted shadow cache");
            }
        }

        if (auto prepared = renderer.prepareExpected(moved, graph, *outputs); !prepared)
        {
            return std::unexpected(prepared.error());
        }
        commands = (*frame)->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        if (auto recorded = graph.executeExpected(*commands); !recorded)
        {
            return std::unexpected(recorded.error());
        }
        if (auto submitted = (*frame)->submitExpected(); !submitted)
        {
            return std::unexpected(submitted.error());
        }
        const auto wait          = device.core.WaitForFence;
        device.core.WaitForFence = [](VriFence*, uint64_t, uint64_t)
        {
            return VriResult_Timeout;
        };
        auto timeout             = (*frame)->waitExpected(0);
        device.core.WaitForFence = wait;
        if (timeout || (*frame)->discardExpected())
        {
            return std::unexpected("Accepted built-in work was discardable after a timeout");
        }
        if (auto completed = (*frame)->waitExpected(); !completed)
        {
            return std::unexpected(completed.error());
        }
        renderer.completeFrame();
        auto movedPixels = capture();
        recovered        = render(moved);
        if (!movedPixels || !recovered || *movedPixels != *recovered || shadowCount())
        {
            return std::unexpected("Completed timed-out submission did not publish its new shadow cache");
        }

        // A rejected native pipeline must not publish the first two of four opaque variants.
        struct RestoreCertificates
        {
            GpuScene&            scene;
            std::vector<uint8_t> previous;

            ~RestoreCertificates()
            {
                (void)scene.setImmutableOpaqueAlpha(previous);
            }
        } certificates {scene, {}};

        for (uint32_t i = 0; i < scene.materials.size(); ++i)
        {
            certificates.previous.push_back(scene.hasImmutableOpaqueAlpha(i));
        }
        std::vector<uint8_t> opaque(scene.materials.size(), 1);
        if (auto marked = scene.setImmutableOpaqueAlpha(opaque); !marked)
        {
            return std::unexpected(marked.error());
        }
        renderer.settings.path = RenderPath::eNaiveForward;
        RenderGraph forward(device);
        auto        prefix = forward.importResourceExpected("prefix", *environment.radiance);
        if (!prefix)
        {
            return std::unexpected(prefix.error());
        }
        if (auto exported = forward.exportResourceExpected(*prefix); !exported)
        {
            return std::unexpected(exported.error());
        }
        if (auto compiled = forward.compileExpected(); !compiled)
        {
            return std::unexpected(compiled.error());
        }
        const auto                       prefixSize = forward.snapshot().resources.size();
        builtin_owner_probe::NativeFault native(device);
        native.arm(builtin_owner_probe::Failure::ePipeline, 3);
        auto rejectedForward = renderer.addScenePassesExpected(forward, size);
        if (rejectedForward || !native.injected || !native.owners.empty() ||
            forward.snapshot().resources.size() != prefixSize || !forward.snapshot().passes.empty())
        {
            return std::unexpected("Rejected opaque variants retained native pipelines or appended graph state");
        }
        native.arm(builtin_owner_probe::Failure::ePipeline, 5);
        const auto calls          = native.creates;
        auto       forwardOutputs = renderer.addPassesExpected(forward, size);
        if (!forwardOutputs || native.injected || native.creates - calls != 4)
        {
            return std::unexpected("Opaque variant retry did not rebuild all four pipelines");
        }
        native.arm(builtin_owner_probe::Failure::eNone);
        if (auto exported = forward.exportResourceExpected(forwardOutputs->color); !exported)
        {
            return std::unexpected(exported.error());
        }
        if (auto compiled = forward.compileExpected(); !compiled)
        {
            return std::unexpected(compiled.error());
        }
        if (auto prepared = renderer.prepareExpected(camera, forward, *forwardOutputs); !prepared)
        {
            return std::unexpected(prepared.error());
        }
        commands = (*frame)->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        if (auto recorded = forward.executeExpected(*commands); !recorded)
        {
            return std::unexpected(recorded.error());
        }
        if (auto completed = (*frame)->submitAndWaitExpected(); !completed)
        {
            return std::unexpected(completed.error());
        }
        renderer.completeFrame();
        auto color = forward.getTextureExpected(forwardOutputs->color);
        if (!color || readback(device, **color).rgba.empty())
        {
            return std::unexpected("Recovered opaque forward graph did not render");
        }
        if (auto polled = renderer.pollShadersExpected(); !polled)
        {
            return std::unexpected(polled.error());
        }
        std::puts("Expected built-in frames: append/prepare rejection, warm shadows, recording/discard/end/submit "
                  "rollback, accepted timeout and atomic opaque-variant recovery passed");
        return {};
    }
} // namespace vultra::test
