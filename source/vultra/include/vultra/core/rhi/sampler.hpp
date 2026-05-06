#pragma once

#include "vultra/core/rhi/structs/sampler_info.hpp"

#include <utility>

namespace vultra
{
    namespace rhi
    {
        // Sampler description used by backend-specific render APIs.
        class Sampler final
        {
        public:
            Sampler() = default;

            explicit Sampler(SamplerInfo info) : m_Info(std::move(info)), m_Valid(true) {}

            [[nodiscard]] explicit operator bool() const { return m_Valid; }

            [[nodiscard]] const SamplerInfo& info() const { return m_Info; }
            [[nodiscard]] SamplerInfo&       info() { return m_Info; }

        private:
            SamplerInfo m_Info {};
            bool        m_Valid {false};
        };
    } // namespace rhi
} // namespace vultra
