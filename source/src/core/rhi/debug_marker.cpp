#if defined(RHI_USE_DEBUG_MARKER) || _DEBUG

#include "vultra/core/rhi/debug_marker.hpp"
#include "vultra/core/rhi/command_buffer.hpp"

namespace vultra
{
    namespace rhi
    {
        DebugMarker::DebugMarker(CommandBuffer& cb, const std::string_view label) : m_CommandBuffer {cb}
        {
            m_CommandBuffer.pushDebugGroup(label);
        }
        DebugMarker::~DebugMarker() { m_CommandBuffer.popDebugGroup(); }
    } // namespace rhi
} // namespace vultra
#endif