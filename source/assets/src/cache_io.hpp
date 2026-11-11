#pragma once

#include <vultra/core/base/error.hpp>

#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <type_traits>
#include <vector>

namespace vultra::asset_detail
{
    // Local derived data, not an exchange format. The pipeline version owns this binary layout.
    class CacheWriter
    {
    public:
        template<typename T>
        void value(const T& value)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            const auto bytes = std::as_bytes(std::span(&value, 1));
            data.insert(data.end(), bytes.begin(), bytes.end());
        }

        template<typename T>
        void array(std::span<const T> values)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            value(uint64_t(values.size()));
            const auto bytes = std::as_bytes(values);
            data.insert(data.end(), bytes.begin(), bytes.end());
        }

        template<typename T>
        void array(const std::vector<T>& values)
        {
            array(std::span<const T>(values));
        }

        std::vector<std::byte> data;
    };

    class CacheReader
    {
    public:
        explicit CacheReader(std::span<const std::byte> bytes) :
            m_Remaining(bytes)
        {
        }

        template<typename T>
        [[nodiscard]] std::expected<T, Error> value()
        {
            static_assert(std::is_trivially_copyable_v<T>);
            if (m_Remaining.size() < sizeof(T))
            {
                return makeError("Truncated asset cache");
            }
            T result;
            std::memcpy(&result, m_Remaining.data(), sizeof(T));
            m_Remaining = m_Remaining.subspan(sizeof(T));
            return result;
        }

        template<typename T>
        [[nodiscard]] std::expected<std::vector<T>, Error> array()
        {
            const auto bytes = arrayBytes<T>();
            if (!bytes)
            {
                return std::unexpected(bytes.error());
            }
            std::vector<T> result(bytes->size() / sizeof(T));
            if (!bytes->empty())
            {
                std::memcpy(result.data(), bytes->data(), bytes->size());
            }
            return result;
        }

        // Keep byte views unaligned. Callers may copy independent payloads after indexing the archive.
        template<typename T>
        [[nodiscard]] std::expected<std::span<const std::byte>, Error> arrayBytes()
        {
            static_assert(std::is_trivially_copyable_v<T>);
            const auto count = value<uint64_t>();
            if (!count)
            {
                return std::unexpected(count.error());
            }
            if (*count > m_Remaining.size() / sizeof(T))
            {
                return makeError("Invalid asset cache array length");
            }
            const auto result = m_Remaining.first(size_t(*count) * sizeof(T));
            m_Remaining       = m_Remaining.subspan(result.size());
            return result;
        }

        bool empty() const
        {
            return m_Remaining.empty();
        }

    private:
        std::span<const std::byte> m_Remaining;
    };
} // namespace vultra::asset_detail
