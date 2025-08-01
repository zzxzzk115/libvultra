#include "vultra/core/rhi/frame_index.hpp"

#include <cassert>
#include <utility>

namespace vultra
{
    namespace rhi
    {
        FrameIndex::FrameIndex(const FrameIndex::ValueType numFramesInFlight) : m_NumFramesInFlight {numFramesInFlight}
        {}

        void FrameIndex::operator++()
        {
            assert(m_NumFramesInFlight != 0);
            m_Previous = std::exchange(m_Index, (m_Index + 1) % m_NumFramesInFlight);
        }
        FrameIndex::operator ValueType() const { return m_Index; }

        FrameIndex::ValueType FrameIndex::getCurrentIndex() const { return m_Index; }
        FrameIndex::ValueType FrameIndex::getPreviousIndex() const { return m_Previous; }
    } // namespace rhi
} // namespace vultra