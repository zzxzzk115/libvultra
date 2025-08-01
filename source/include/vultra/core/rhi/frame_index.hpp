#pragma once

#include <cstdint>

namespace vultra
{
    namespace rhi
    {
        struct FrameIndex
        {
            using ValueType = uint8_t;

            explicit FrameIndex(const ValueType numFramesInFlight = 0);

            void operator++();
            // Implicit conversion operator for convenience.
            [[nodiscard]] operator ValueType() const;

            [[nodiscard]] ValueType getCurrentIndex() const;
            [[nodiscard]] ValueType getPreviousIndex() const;

        private:
            ValueType m_NumFramesInFlight {0};
            ValueType m_Index {0};
            ValueType m_Previous {0};
        };
    } // namespace rhi
} // namespace vultra