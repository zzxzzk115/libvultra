#include <vultra/servers/rendering/graph/render_graph.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <numeric>

namespace
{
    using namespace vultra;

    struct Region
    {
        uint32_t            mip;
        uint32_t            layer;
        uint32_t            width;
        uint32_t            height;
        VriImageAspectFlags aspect;
        uint32_t            pixelBytes;
        uint32_t            rowBytes;
        uint64_t            offset;
    };

    float depthValue(const Region& region, uint32_t phase, uint32_t x, uint32_t y)
    {
        float value = .25f + float(region.layer) * .125f + float(region.mip) * .03125f;
        if (phase)
        {
            value += .25f + float((x + y) % 4) / 128;
        }
        return value;
    }

    uint8_t stencilValue(const Region& region, uint32_t phase, uint32_t x, uint32_t y)
    {
        uint32_t value = 17 + region.layer * 3 + region.mip;
        if (phase == 2)
        {
            value += 32 + (x + 2 * y) % 8;
        }
        return uint8_t(value);
    }

    VriBufferTextureCopyDesc copyRegion(const Region& region)
    {
        VriBufferTextureCopyDesc copy {};
        copy.bufferOffset    = region.offset;
        copy.bufferRowLength = region.rowBytes / region.pixelBytes;
        copy.texture         = {region.mip, region.layer, 1, region.aspect, 0, 0, 0, region.width, region.height, 1};
        return copy;
    }

    std::expected<void, std::string> verifyGraph(Device&                 device,
                                                 Texture&                texture,
                                                 Buffer&                 upload,
                                                 Buffer&                 output,
                                                 Frame&                  frame,
                                                 std::span<const Region> regions,
                                                 uint64_t                size)
    {
        RenderGraph graph(device);
        auto        image       = graph.importResourceExpected("combined", texture);
        auto        source      = graph.importResourceExpected("upload", upload);
        auto        destination = graph.importResourceExpected("readback", output);
        if (!image || !source || !destination)
        {
            return std::unexpected("Import aspect graph resources failed");
        }
        const RenderGraph::TextureRange depth {.aspect = VriImageAspect_Depth};
        const RenderGraph::TextureRange stencil {.aspect = VriImageAspect_Stencil};
        bool                            obsoleteCalled = false;
        bool                            fail           = false;
        const std::array obsoleteUses {RenderGraph::Use {*image, Usage::eCopyDestination, true, stencil}};
        if (!graph.addPassExpected("obsolete stencil",
                                   obsoleteUses,
                                   [&](auto*, auto&) -> std::expected<void, std::string>
                                   {
                                       obsoleteCalled = true;
                                       return {};
                                   }))
        {
            return std::unexpected("Add obsolete stencil producer failed");
        }
        const std::array depthUses {RenderGraph::Use {*source, Usage::eCopySource},
                                    RenderGraph::Use {*image, Usage::eCopyDestination, true, depth}};
        if (!graph.addPassExpected(
                "depth upload",
                depthUses,
                [&](auto* commands, auto&) -> std::expected<void, std::string>
                {
                    for (const auto& region : regions)
                    {
                        if (region.aspect == VriImageAspect_Depth)
                        {
                            const auto copy = copyRegion(region);
                            device.core.CmdUploadBufferToTexture(commands, texture.handle, upload.handle, &copy);
                        }
                    }
                    return {};
                }))
        {
            return std::unexpected("Add depth producer failed");
        }
        const std::array mixedUses {RenderGraph::Use {*source, Usage::eCopySource},
                                    RenderGraph::Use {*image, Usage::eCopySource, false, depth},
                                    RenderGraph::Use {*image, Usage::eCopyDestination, true, stencil},
                                    RenderGraph::Use {*destination, Usage::eCopyDestination}};
        if (!graph.addPassExpected(
                "read depth and write stencil",
                mixedUses,
                [&](auto* commands, auto&) -> std::expected<void, std::string>
                {
                    const auto a = texture.stateExpected(0, 0, VriImageAspect_Depth);
                    const auto b = texture.stateExpected(0, 0, VriImageAspect_Stencil);
                    if (!a || !b || a->layout != VriLayout_CopySource || b->layout != VriLayout_CopyDestination ||
                        texture.stateExpected(0, 0))
                    {
                        return std::unexpected("Graph did not preserve independent aspect layouts");
                    }
                    for (const auto& region : regions)
                    {
                        const auto copy = copyRegion(region);
                        if (region.aspect == VriImageAspect_Depth)
                        {
                            device.core.CmdReadbackTextureToBuffer(commands, output.handle, texture.handle, &copy);
                        }
                        else
                        {
                            device.core.CmdUploadBufferToTexture(commands, texture.handle, upload.handle, &copy);
                        }
                    }
                    return {};
                }))
        {
            return std::unexpected("Add mixed aspect pass failed");
        }
        const std::array readUses {RenderGraph::Use {*image, Usage::eCopySource, false, stencil},
                                   RenderGraph::Use {*destination, Usage::eCopyDestination}};
        if (!graph.addPassExpected(
                "stencil readback",
                readUses,
                [&](auto* commands, auto&) -> std::expected<void, std::string>
                {
                    for (const auto& region : regions)
                    {
                        if (region.aspect == VriImageAspect_Stencil)
                        {
                            const auto copy = copyRegion(region);
                            device.core.CmdReadbackTextureToBuffer(commands, output.handle, texture.handle, &copy);
                        }
                    }
                    if (fail)
                    {
                        return std::unexpected("injected aspect recording failure");
                    }
                    return {};
                }) ||
            !graph.exportResourceExpected(*destination) || !graph.compileExpected())
        {
            return std::unexpected("Compile aspect graph failed");
        }
        const auto names = graph.activePasses();
        if (std::ranges::find(names, "obsolete stencil") != names.end() || names.size() != 3)
        {
            return std::unexpected("Stencil overwrite changed the independent depth producer's liveness");
        }
        const auto prepare = [&](uint32_t generation) -> std::expected<void, std::string>
        {
            auto* data = static_cast<std::byte*>(device.core.MapBuffer(upload.handle, 0, size));
            if (!data)
            {
                return std::unexpected("Map graph aspect upload failed");
            }
            for (const auto& region : regions)
            {
                for (uint32_t y = 0; y < region.height; ++y)
                {
                    for (uint32_t x = 0; x < region.width; ++x)
                    {
                        auto* pixel = data + region.offset + uint64_t(y) * region.rowBytes + x * region.pixelBytes;
                        if (region.aspect == VriImageAspect_Depth)
                        {
                            const float value = depthValue(region, 1, x, y) + float(generation) / 64;
                            std::memcpy(pixel, &value, sizeof(value));
                        }
                        else
                        {
                            *pixel = std::byte(stencilValue(region, 2, x, y) + generation * 3);
                        }
                    }
                }
            }
            device.core.UnmapBuffer(upload.handle);
            return {};
        };
        uint64_t   checks = 0;
        const auto check  = [&](uint32_t generation) -> std::expected<void, std::string>
        {
            const auto* data = static_cast<const std::byte*>(device.core.MapBuffer(output.handle, 0, size));
            if (!data)
            {
                return std::unexpected("Map graph aspect readback failed");
            }
            bool valid = true;
            for (const auto& region : regions)
            {
                for (uint32_t y = 0; y < region.height; ++y)
                {
                    for (uint32_t x = 0; x < region.width; ++x)
                    {
                        const auto* pixel =
                            data + region.offset + uint64_t(y) * region.rowBytes + x * region.pixelBytes;
                        if (region.aspect == VriImageAspect_Depth)
                        {
                            float actual = 0;
                            std::memcpy(&actual, pixel, sizeof(actual));
                            valid &= actual == depthValue(region, 1, x, y) + float(generation) / 64;
                        }
                        else
                        {
                            valid &= std::to_integer<uint8_t>(*pixel) == stencilValue(region, 2, x, y) + generation * 3;
                        }
                        ++checks;
                    }
                }
            }
            device.core.UnmapBuffer(output.handle);
            if (!valid)
            {
                return std::unexpected("Graph aspect data changed at generation " + std::to_string(generation));
            }
            return {};
        };
        const auto readCurrent = [&](uint32_t generation) -> std::expected<void, std::string>
        {
            auto commands = frame.beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            texture.transition(*commands, {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer});
            output.transition(*commands, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
            for (const auto& region : regions)
            {
                const auto copy = copyRegion(region);
                device.core.CmdReadbackTextureToBuffer(*commands, output.handle, texture.handle, &copy);
            }
            if (auto submitted = frame.submitAndWaitExpected(); !submitted)
            {
                return submitted;
            }
            return check(generation);
        };
        for (uint32_t generation = 1; generation <= 3; ++generation)
        {
            if (auto prepared = prepare(generation); !prepared)
            {
                return prepared;
            }
            if (generation > 1)
            {
                auto       checkpoint = graph.checkpointExpected();
                const auto state      = texture.captureState();
                auto       commands   = frame.beginExpected();
                if (!checkpoint || !commands)
                {
                    return std::unexpected("Capture aspect discard checkpoint failed");
                }
                fail          = generation == 2;
                auto recorded = graph.executeExpected(*commands);
                if (recorded.has_value() == fail)
                {
                    return std::unexpected("Injected aspect callback failure was not propagated");
                }
                if (!frame.discardExpected())
                {
                    return std::unexpected("Discard aspect recording failed");
                }
                if (recorded && !graph.restoreUnsubmittedExpected(std::move(*checkpoint)))
                {
                    return std::unexpected("Restore successful unsubmitted aspect recording failed");
                }
                if (texture.captureState() != state)
                {
                    return std::unexpected("Aspect recording failure/discard did not restore every plane");
                }
                if (auto unchanged = readCurrent(generation - 1); !unchanged)
                {
                    return unchanged;
                }
            }
            fail          = false;
            auto commands = frame.beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            if (auto recorded = graph.executeExpected(*commands); !recorded)
            {
                return recorded;
            }
            if (auto submitted = frame.submitAndWaitExpected(); !submitted)
            {
                return submitted;
            }
            if (auto values = check(generation); !values)
            {
                return values;
            }
        }
        if (obsoleteCalled)
        {
            return std::unexpected("Culled stencil callback executed");
        }
        for (const auto aspect : {VriImageAspect_Depth, VriImageAspect_Stencil})
        {
            const auto chosen =
                std::ranges::find_if(regions,
                                     [aspect](const Region& region)
                                     {
                                         return region.mip == 1 && region.layer == 1 && region.aspect == aspect;
                                     });
            if (chosen == regions.end())
            {
                return std::unexpected("Missing selected aspect fixture cell");
            }
            RenderGraph capture(device);
            auto        source = capture.importResourceExpected("selected aspect source", texture);
            if (!source)
            {
                return std::unexpected(source.error());
            }
            const RenderGraph::TextureRange selected {chosen->mip, 1, chosen->layer, 1, aspect};
            const std::array writes {RenderGraph::Use {*source, Usage::eCopyDestination, false, selected}};
            if (auto added = capture.addPassExpected("preserve selected plane",
                                                     writes,
                                                     [](auto*, auto&) -> std::expected<void, std::string>
                                                     {
                                                         return {};
                                                     });
                !added)
            {
                return added;
            }
            auto resource = capture.captureRangeAfterPassExpected("preserve selected plane",
                                                                  {*source, selected},
                                                                  "selected aspect");
            if (!resource)
            {
                return std::unexpected(resource.error());
            }
            if (auto compiled = capture.compileHeapsExpected(); !compiled)
            {
                return compiled;
            }
            auto plan  = capture.snapshotExpected();
            auto image = capture.getTextureExpected(*resource);
            if (!plan || !image || plan->exports.size() != 1 || plan->exports[0].range.aspect != aspect ||
                (*image)->desc.format != texture.desc.format || (*image)->desc.width != chosen->width ||
                (*image)->desc.height != chosen->height)
            {
                return std::unexpected("Incorrect selected-aspect export");
            }
            auto commands = frame.beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            if (auto recorded = capture.executeExpected(*commands); !recorded)
            {
                return recorded;
            }
            const auto sibling    = aspect == VriImageAspect_Depth ? VriImageAspect_Stencil : VriImageAspect_Depth;
            auto       unselected = (*image)->stateExpected(0, 0, sibling);
            if (!unselected || unselected->layout != VriLayout_Undefined)
            {
                return std::unexpected("Snapshot initialized an unselected plane");
            }
            VriTextureViewDesc view {};
            view.viewType = VriTextureViewType_MaxEnum;
            view.aspect   = aspect;
            if (auto transitioned = (*image)->transitionExpected(
                    *commands,
                    {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer},
                    view);
                !transitioned)
            {
                return transitioned;
            }
            output.transition(*commands, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
            auto copy              = copyRegion(*chosen);
            copy.texture.mip       = 0;
            copy.texture.baseLayer = 0;
            device.core.CmdReadbackTextureToBuffer(*commands, output.handle, (*image)->handle, &copy);
            if (auto submitted = frame.submitAndWaitExpected(); !submitted)
            {
                return submitted;
            }
            const auto* data = static_cast<const std::byte*>(device.core.MapBuffer(output.handle, 0, size));
            if (!data)
            {
                return std::unexpected("Map selected snapshot readback failed");
            }
            bool exact = true;
            for (uint32_t y = 0; y < chosen->height; ++y)
            {
                for (uint32_t x = 0; x < chosen->width; ++x)
                {
                    const auto* pixel = data + chosen->offset + uint64_t(y) * chosen->rowBytes + x * chosen->pixelBytes;
                    if (aspect == VriImageAspect_Depth)
                    {
                        float value = 0;
                        std::memcpy(&value, pixel, sizeof(value));
                        exact &= value == depthValue(*chosen, 1, x, y) + 3.f / 64;
                    }
                    else
                    {
                        exact &= std::to_integer<uint8_t>(*pixel) == stencilValue(*chosen, 2, x, y) + 9;
                    }
                }
            }
            device.core.UnmapBuffer(output.handle);
            if (!exact)
            {
                return std::unexpected("Selected aspect snapshot differs from the original plane");
            }
            std::printf("GRAPH_ASPECT_SNAPSHOT aspect=%u values=%u sibling_undefined=1 exact=1\n",
                        unsigned(aspect),
                        chosen->width * chosen->height);
        }
        std::printf("Graph aspects: %llu exact values, independent mixed layouts, failure/discard/retry\n",
                    static_cast<unsigned long long>(checks));
        return {};
    }

    std::expected<void, std::string> verifyImageCopies(Device&                 device,
                                                       Texture&                source,
                                                       Buffer&                 upload,
                                                       Buffer&                 output,
                                                       std::span<const Region> regions,
                                                       uint64_t                size)
    {
        auto target = Texture::create(device, source.desc);
        auto frame  = Frame::create(device);
        if (!target || !frame)
        {
            return std::unexpected("Create selected-aspect copy target/frame failed");
        }
        for (uint32_t phase = 0; phase < 2; ++phase)
        {
            if (phase)
            {
                auto* data = static_cast<std::byte*>(device.core.MapBuffer(upload.handle, 0, size));
                if (!data)
                {
                    return std::unexpected("Map distinct source depth failed");
                }
                for (const auto& region : regions)
                {
                    if (region.aspect != VriImageAspect_Depth)
                    {
                        continue;
                    }
                    for (uint32_t y = 0; y < region.height; ++y)
                    {
                        for (uint32_t x = 0; x < region.width; ++x)
                        {
                            const float value = depthValue(region, 1, x, y) + 4.f / 64;
                            std::memcpy(data + region.offset + uint64_t(y) * region.rowBytes + x * 4,
                                        &value,
                                        sizeof(value));
                        }
                    }
                }
                device.core.UnmapBuffer(upload.handle);
            }
            auto commands = (*frame)->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            if (!phase)
            {
                (*target)->transition(*commands,
                                      {VriAccess_DepthStencilAttachmentWrite,
                                       VriLayout_DepthStencilAttachment,
                                       VriPipelineStage_EarlyFragmentTests | VriPipelineStage_LateFragmentTests});
                for (const auto& region : regions)
                {
                    if (region.aspect != VriImageAspect_Depth)
                    {
                        continue;
                    }
                    VriTextureViewDesc view {};
                    view.viewType   = VriTextureViewType_2D;
                    view.baseMip    = region.mip;
                    view.mipNum     = 1;
                    view.baseLayer  = region.layer;
                    view.layerNum   = 1;
                    view.aspect     = VriImageAspect_Depth | VriImageAspect_Stencil;
                    auto descriptor = (*target)->viewExpected(view);
                    if (!descriptor)
                    {
                        return std::unexpected(descriptor.error());
                    }
                    VriAttachmentDesc attachment {};
                    attachment.view                            = *descriptor;
                    attachment.loadOp                          = VriAttachmentLoadOp_Clear;
                    attachment.storeOp                         = VriAttachmentStoreOp_Store;
                    attachment.clearValue.depthStencil.depth   = .875f;
                    attachment.clearValue.depthStencil.stencil = 145 + region.layer * 3 + region.mip;
                    VriAttachmentsDesc attachments {};
                    attachments.depth      = &attachment;
                    attachments.stencil    = &attachment;
                    attachments.renderArea = {0, 0, region.width, region.height};
                    attachments.layerNum   = 1;
                    device.core.CmdBeginRendering(*commands, &attachments);
                    device.core.CmdEndRendering(*commands);
                }
            }
            else
            {
                upload.transition(*commands, {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
                VriTextureViewDesc view {};
                view.viewType = VriTextureViewType_MaxEnum;
                view.aspect   = VriImageAspect_Depth;
                if (auto changed = source.transitionExpected(
                        *commands,
                        {VriAccess_CopyDestinationWrite, VriLayout_CopyDestination, VriPipelineStage_Transfer},
                        view);
                    !changed)
                {
                    return changed;
                }
                for (const auto& region : regions)
                {
                    if (region.aspect == VriImageAspect_Depth)
                    {
                        const auto copy = copyRegion(region);
                        device.core.CmdUploadBufferToTexture(*commands, source.handle, upload.handle, &copy);
                    }
                }
            }
            source.transition(*commands, {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer});
            const auto         selected = phase ? VriImageAspect_Stencil : VriImageAspect_Depth;
            VriTextureViewDesc view {};
            view.viewType = VriTextureViewType_MaxEnum;
            view.aspect   = selected;
            if (auto changed = (*target)->transitionExpected(
                    *commands,
                    {VriAccess_CopyDestinationWrite, VriLayout_CopyDestination, VriPipelineStage_Transfer},
                    view);
                !changed)
            {
                return changed;
            }
            for (const auto& region : regions)
            {
                if (region.aspect == selected)
                {
                    VriTextureCopyDesc copy {};
                    copy.src = copyRegion(region).texture;
                    copy.dst = copy.src;
                    device.core.CmdCopyTexture(*commands, (*target)->handle, source.handle, &copy);
                }
            }
            (*target)->transition(*commands,
                                  {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer});
            output.transition(*commands, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
            for (const auto& region : regions)
            {
                const auto copy = copyRegion(region);
                device.core.CmdReadbackTextureToBuffer(*commands, output.handle, (*target)->handle, &copy);
            }
            if (auto submitted = (*frame)->submitAndWaitExpected(); !submitted)
            {
                return submitted;
            }
            const auto* data = static_cast<const std::byte*>(device.core.MapBuffer(output.handle, 0, size));
            if (!data)
            {
                return std::unexpected("Map selected-aspect texture copy readback failed");
            }
            bool valid = true;
            for (const auto& region : regions)
            {
                for (uint32_t y = 0; y < region.height; ++y)
                {
                    for (uint32_t x = 0; x < region.width; ++x)
                    {
                        const auto* pixel =
                            data + region.offset + uint64_t(y) * region.rowBytes + x * region.pixelBytes;
                        if (region.aspect == VriImageAspect_Depth)
                        {
                            float actual = 0;
                            std::memcpy(&actual, pixel, sizeof(actual));
                            const float expected = depthValue(region, 1, x, y) + 3.f / 64;
                            if (valid && actual != expected)
                            {
                                std::fprintf(stderr,
                                             "Image-copy depth mismatch: phase=%u mip=%u layer=%u xy=%u,%u actual=%g "
                                             "expected=%g\n",
                                             phase,
                                             region.mip,
                                             region.layer,
                                             x,
                                             y,
                                             actual,
                                             expected);
                            }
                            valid &= actual == expected;
                        }
                        else
                        {
                            uint32_t expected = 145 + region.layer * 3 + region.mip;
                            if (phase)
                            {
                                expected = stencilValue(region, 2, x, y) + 9;
                            }
                            const auto actual = std::to_integer<uint8_t>(*pixel);
                            if (valid && actual != expected)
                            {
                                std::fprintf(stderr,
                                             "Image-copy stencil mismatch: phase=%u mip=%u layer=%u xy=%u,%u actual=%u "
                                             "expected=%u\n",
                                             phase,
                                             region.mip,
                                             region.layer,
                                             x,
                                             y,
                                             actual,
                                             expected);
                            }
                            valid &= actual == expected;
                        }
                    }
                }
            }
            device.core.UnmapBuffer(output.handle);
            if (!valid)
            {
                return std::unexpected("Selected texture copy overwrote the other aspect: phase " +
                                       std::to_string(phase));
            }
        }
        std::puts("Selected depth/stencil image copies preserve the other plane: 640 exact values");
        return {};
    }

    std::expected<void, std::string> verifyUnsupportedCopy(Device& device, VriTextureDesc description)
    {
        const auto api                = device.core.GetDeviceDesc(device.handle)->graphicsAPI;
        bool       rejectsMultisample = api == VriGraphicsAPI_Metal;
#if defined(__APPLE__)
        // macOS Vulkan uses MoltenVK; both implementations stage a selected plane through a buffer.
        rejectsMultisample |= api == VriGraphicsAPI_Vulkan;
#endif
        if (!rejectsMultisample)
        {
            return {};
        }
        description.type      = VriTextureType_2D;
        description.mipNum    = 1;
        description.layerNum  = 1;
        description.sampleNum = 4;
        auto source           = Texture::create(device, description);
        auto destination      = Texture::create(device, description);
        auto frame            = Frame::create(device);
        if (!source || !destination || !frame)
        {
            return std::unexpected("Create unsupported multisample plane-copy control failed");
        }
        auto commands = (*frame)->beginExpected();
        if (!commands)
        {
            return std::unexpected(commands.error());
        }
        VriTextureCopyDesc copy {};
        copy.src = {0, 0, 1, VriImageAspect_Depth, 0, 0, 0, description.width, description.height, 1};
        copy.dst = copy.src;
        device.core.CmdCopyTexture(*commands, (*destination)->handle, (*source)->handle, &copy);
        if ((*frame)->submitAndWaitExpected() || !(*frame)->discardExpected())
        {
            return std::unexpected("Unsupported multisample plane copy submitted or could not be discarded");
        }
        auto retry = (*frame)->beginExpected();
        if (!retry || !(*frame)->submitAndWaitExpected())
        {
            return std::unexpected("Rejected native recording poisoned a fresh empty submission");
        }
        std::puts("Unsupported native multisample plane copy returns Expected and permits discarded-frame reuse");
        return {};
    }

    std::expected<void, std::string> verify(Device& device)
    {
        auto desc     = colorTexture({8, 8}, VriFormat_D32_SFLOAT_S8_UINT);
        desc.type     = VriTextureType_2DArray;
        desc.mipNum   = 2;
        desc.layerNum = 2;
        desc.usage = VriTextureUsage_DepthStencilAttachment | VriTextureUsage_TransferSrc | VriTextureUsage_TransferDst;
        auto texture = Texture::create(device, desc);
        if (!texture)
        {
            return std::unexpected(texture.error());
        }
        const auto*    caps      = device.core.GetDeviceDesc(device.handle);
        const uint64_t alignment = std::lcm(uint64_t(4), uint64_t(std::max(caps->uploadBufferTextureRowAlignment, 1u)));
        std::vector<Region> regions;
        uint64_t            size = 0;
        for (uint32_t layer = 0; layer < 2; ++layer)
        {
            for (uint32_t mip = 0; mip < 2; ++mip)
            {
                for (const auto aspect : {VriImageAspect_Depth, VriImageAspect_Stencil})
                {
                    const uint32_t width      = 8u >> mip;
                    const uint32_t pixelBytes = aspect == VriImageAspect_Depth ? 4 : 1;
                    const uint32_t row        = uint32_t((width * pixelBytes + alignment - 1) / alignment * alignment);
                    regions.push_back({mip, layer, width, width, aspect, pixelBytes, row, size});
                    size += uint64_t(row) * width;
                }
            }
        }
        auto upload = Buffer::create(device, {size, 0, VriBufferUsage_TransferSrc, VriMemoryLocation_HostUpload});
        auto output = Buffer::create(device, {size, 0, VriBufferUsage_TransferDst, VriMemoryLocation_HostReadback});
        auto frame  = Frame::create(device);
        if (!upload || !output || !frame)
        {
            return std::unexpected("Create depth/stencil transfer buffers/frame failed");
        }
        auto* initialized = device.core.MapBuffer((*output)->handle, 0, size);
        if (!initialized)
        {
            return std::unexpected("Initialize graph readback buffer failed");
        }
        std::memset(initialized, 0, size_t(size));
        device.core.UnmapBuffer((*output)->handle);
        auto* mapped = static_cast<std::byte*>(device.core.MapBuffer((*upload)->handle, 0, size));
        if (!mapped)
        {
            return std::unexpected("Map depth/stencil upload failed");
        }
        std::memset(mapped, 0, size_t(size));
        for (const auto& region : regions)
        {
            for (uint32_t y = 0; y < region.height; ++y)
            {
                for (uint32_t x = 0; x < region.width; ++x)
                {
                    auto* pixel = mapped + region.offset + uint64_t(y) * region.rowBytes + x * region.pixelBytes;
                    if (region.aspect == VriImageAspect_Depth)
                    {
                        const float value = depthValue(region, 1, x, y);
                        std::memcpy(pixel, &value, sizeof(value));
                    }
                    else
                    {
                        *pixel = std::byte(stencilValue(region, 2, x, y));
                    }
                }
            }
        }
        device.core.UnmapBuffer((*upload)->handle);
        uint64_t checks = 0;
        for (uint32_t phase = 0; phase < 3; ++phase)
        {
            auto commands = (*frame)->beginExpected();
            if (!commands)
            {
                return std::unexpected(commands.error());
            }
            if (!phase)
            {
                (*texture)->transition(*commands,
                                       {VriAccess_DepthStencilAttachmentWrite,
                                        VriLayout_DepthStencilAttachment,
                                        VriPipelineStage_EarlyFragmentTests | VriPipelineStage_LateFragmentTests});
                for (const auto& region : regions)
                {
                    if (region.aspect != VriImageAspect_Depth)
                    {
                        continue;
                    }
                    VriTextureViewDesc view {};
                    view.viewType   = VriTextureViewType_2D;
                    view.baseMip    = region.mip;
                    view.mipNum     = 1;
                    view.baseLayer  = region.layer;
                    view.layerNum   = 1;
                    view.aspect     = VriImageAspect_Depth | VriImageAspect_Stencil;
                    auto descriptor = (*texture)->viewExpected(view);
                    if (!descriptor)
                    {
                        return std::unexpected(descriptor.error());
                    }
                    VriAttachmentDesc attachment {};
                    attachment.view                            = *descriptor;
                    attachment.loadOp                          = VriAttachmentLoadOp_Clear;
                    attachment.storeOp                         = VriAttachmentStoreOp_Store;
                    attachment.clearValue.depthStencil.depth   = depthValue(region, 0, 0, 0);
                    attachment.clearValue.depthStencil.stencil = stencilValue(region, 0, 0, 0);
                    VriAttachmentsDesc attachments {};
                    attachments.depth      = &attachment;
                    attachments.stencil    = &attachment;
                    attachments.renderArea = {0, 0, region.width, region.height};
                    attachments.layerNum   = 1;
                    device.core.CmdBeginRendering(*commands, &attachments);
                    device.core.CmdEndRendering(*commands);
                }
            }
            else
            {
                (*upload)->transition(*commands, {VriAccess_CopySourceRead, VriPipelineStage_Transfer});
                (*texture)->transition(
                    *commands,
                    {VriAccess_CopyDestinationWrite, VriLayout_CopyDestination, VriPipelineStage_Transfer});
                for (const auto& region : regions)
                {
                    if ((phase == 1 && region.aspect == VriImageAspect_Depth) ||
                        (phase == 2 && region.aspect == VriImageAspect_Stencil))
                    {
                        const auto copy = copyRegion(region);
                        device.core.CmdUploadBufferToTexture(*commands, (*texture)->handle, (*upload)->handle, &copy);
                    }
                }
            }
            (*texture)->transition(*commands,
                                   {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer});
            (*output)->transition(*commands, {VriAccess_CopyDestinationWrite, VriPipelineStage_Transfer});
            for (const auto& region : regions)
            {
                const auto copy = copyRegion(region);
                device.core.CmdReadbackTextureToBuffer(*commands, (*output)->handle, (*texture)->handle, &copy);
            }
            if (auto submitted = (*frame)->submitAndWaitExpected(); !submitted)
            {
                return submitted;
            }
            const auto* pixels = static_cast<const std::byte*>(device.core.MapBuffer((*output)->handle, 0, size));
            if (!pixels)
            {
                return std::unexpected("Map depth/stencil readback failed");
            }
            bool valid = true;
            for (const auto& region : regions)
            {
                for (uint32_t y = 0; y < region.height; ++y)
                {
                    for (uint32_t x = 0; x < region.width; ++x)
                    {
                        const auto* pixel =
                            pixels + region.offset + uint64_t(y) * region.rowBytes + x * region.pixelBytes;
                        if (region.aspect == VriImageAspect_Depth)
                        {
                            float actual = 0;
                            std::memcpy(&actual, pixel, sizeof(actual));
                            valid &= actual == depthValue(region, phase, x, y);
                        }
                        else
                        {
                            valid &= std::to_integer<uint8_t>(*pixel) == stencilValue(region, phase, x, y);
                        }
                        ++checks;
                    }
                }
            }
            device.core.UnmapBuffer((*output)->handle);
            if (!valid)
            {
                return std::unexpected("Depth/stencil plane transfer changed data at phase " + std::to_string(phase));
            }
        }
        std::printf("Depth/stencil plane transfers: %llu exact values, two mips and two layers\n",
                    static_cast<unsigned long long>(checks));
        if (auto graph = verifyGraph(device, **texture, **upload, **output, **frame, regions, size); !graph)
        {
            return graph;
        }
        if (auto copied = verifyImageCopies(device, **texture, **upload, **output, regions, size); !copied)
        {
            return copied;
        }
        return verifyUnsupportedCopy(device, desc);
    }
} // namespace

int main()
{
    auto device = vultra::Device::create(true);
    if (!device)
    {
        std::fprintf(stderr, "%s\n", device.error().c_str());
        return 1;
    }
    if (auto result = verify(**device); !result)
    {
        std::fprintf(stderr, "%s\n", result.error().c_str());
        return 1;
    }
    return 0;
}
