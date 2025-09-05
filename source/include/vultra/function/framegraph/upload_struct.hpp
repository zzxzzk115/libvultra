#pragma once

#include "vultra/core/rhi/command_buffer.hpp"
#include "vultra/function/framegraph/framegraph_resource_access.hpp"
#include "vultra/function/framegraph/render_context.hpp"
#include "vultra/function/framegraph/transient_buffer.hpp"

#include <fg/FrameGraph.hpp>

namespace vultra
{
    namespace framegraph
    {
        template<typename T>
        [[nodiscard]] FrameGraphResource
        uploadStruct(FrameGraph& fg, const std::string_view passName, TransientBuffer<T>&& s)
        {
            ZoneTransientN(__tracy_zone, passName.data(), true);

            constexpr auto kDataSize = static_cast<uint32_t>(sizeof(T));
            auto           payload   = std::make_shared<T>(std::move(s.data));

            struct Data
            {
                FrameGraphResource buffer;
            };
            const auto [buffer] = fg.addCallbackPass<Data>(
                passName,
                [&s](FrameGraph::Builder& builder, Data& data) {
                    PASS_SETUP_ZONE;

                    data.buffer = builder.create<FrameGraphBuffer>(s.name,
                                                                   {
                                                                       .type     = s.type,
                                                                       .stride   = kDataSize,
                                                                       .capacity = 1,
                                                                   });
                    data.buffer = builder.write(data.buffer, BindingInfo {.pipelineStage = PipelineStage::eTransfer});
                },
                [passName, payload](const Data& data, FrameGraphPassResources& resources, void* ctx) {
                    auto& cb = static_cast<RenderContext*>(ctx)->commandBuffer;
                    RHI_GPU_ZONE(cb, passName.data());
                    cb.update(*resources.get<FrameGraphBuffer>(data.buffer).buffer, 0, kDataSize, payload.get());
                });

            return buffer;
        }
    } // namespace framegraph
} // namespace vultra