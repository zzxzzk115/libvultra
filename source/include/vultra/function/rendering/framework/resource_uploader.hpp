#pragma once

#include "vultra/function/framegraph/transient_buffer.hpp"
#include "vultra/function/framegraph/upload_struct.hpp"
#include "vultra/function/rendering/framework/render_frame_resources.hpp"
#include "vultra/function/rendering/framework/uploaded_buffer.hpp"

#include <type_traits>
#include <utility>

namespace vultra
{
    class FrameGraphResourceUploader
    {
    public:
        explicit FrameGraphResourceUploader(FrameGraph& fg) : m_FG(fg) {}

        template<typename T>
        [[nodiscard]] UploadedBuffer
        uploadStruct(std::string_view passName, std::string_view resourceName, framegraph::BufferType type, T&& data)
        {
            UploadedBuffer result;
            result.kind       = UploadedBuffer::Kind::eFrameGraph;
            result.fgResource = framegraph::uploadStruct(m_FG,
                                                         passName,
                                                         framegraph::TransientBuffer<std::remove_cvref_t<T>> {
                                                             .name = resourceName,
                                                             .type = type,
                                                             .data = std::forward<T>(data),
                                                         });
            return result;
        }

    private:
        FrameGraph& m_FG;
    };

    class ImmediateResourceUploader
    {
    public:
        ImmediateResourceUploader(RenderFrameResources& resources, rhi::RenderDevice& rd) :
            m_Resources(resources), m_RD(rd)
        {}

        template<typename T>
        [[nodiscard]] UploadedBuffer
        uploadStruct(std::string_view, std::string_view, framegraph::BufferType type, T&& data)
        {
            UploadedBuffer result;
            result.kind = UploadedBuffer::Kind::eImmediate;

            if (type == framegraph::BufferType::eUniformBuffer)
                result.buffer = m_Resources.uploadUniform(m_RD, data);
            else
                result.buffer = m_Resources.uploadStorage(m_RD, &data, 1);

            return result;
        }

    private:
        RenderFrameResources& m_Resources;
        rhi::RenderDevice&    m_RD;
    };
} // namespace vultra
