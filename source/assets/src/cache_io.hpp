#pragma once

#include <cstdint>
#include <cstring>
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
        bool read(T& value)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            if (m_Remaining.size() < sizeof(T))
            {
                return false;
            }
            std::memcpy(&value, m_Remaining.data(), sizeof(T));
            m_Remaining = m_Remaining.subspan(sizeof(T));
            return true;
        }

        template<typename T>
        bool readArray(std::vector<T>& values)
        {
            std::span<const std::byte> bytes;
            if (!readArrayBytes<T>(bytes))
            {
                return false;
            }
            values.resize(bytes.size() / sizeof(T));
            if (!bytes.empty())
            {
                std::memcpy(values.data(), bytes.data(), bytes.size());
            }
            return true;
        }

        // Keep byte views unaligned. Callers may copy independent payloads after indexing the archive.
        template<typename T>
        bool readArrayBytes(std::span<const std::byte>& bytes)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            uint64_t count;
            if (!read(count) || count > m_Remaining.size() / sizeof(T))
            {
                return false;
            }
            bytes       = m_Remaining.first(size_t(count) * sizeof(T));
            m_Remaining = m_Remaining.subspan(bytes.size());
            return true;
        }

        bool empty() const
        {
            return m_Remaining.empty();
        }

    private:
        std::span<const std::byte> m_Remaining;
    };
} // namespace vultra::asset_detail
