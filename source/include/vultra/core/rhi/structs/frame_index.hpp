#pragma once

#include <cstdint>

namespace vultra
{
    namespace rhi
    {
        struct FrameIndex
        {
            using ValueType = uint32_t;

            explicit FrameIndex(ValueType numFramesInFlight = 0);

            void operator++();
            operator ValueType() const;

            [[nodiscard]] ValueType getCurrentIndex() const;
            [[nodiscard]] ValueType getPreviousIndex() const;

        private:
            ValueType m_Index {0};
            ValueType m_Previous {0};
            ValueType m_NumFramesInFlight {0};
        };
    } // namespace rhi
} // namespace vultra
