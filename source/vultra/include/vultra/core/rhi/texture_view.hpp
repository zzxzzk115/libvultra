#pragma once

#include <cstdint>

namespace vultra
{
    namespace rhi
    {
        class TextureView final
        {
        public:
            TextureView() = default;
            explicit TextureView(std::uintptr_t handle) : m_Handle(handle) {}

            [[nodiscard]] explicit operator bool() const { return m_Handle != 0; }
            [[nodiscard]] std::uintptr_t getHandle() const { return m_Handle; }

        private:
            std::uintptr_t m_Handle {0};
        };
    } // namespace rhi
} // namespace vultra
