#pragma once

#include <vultra/core/base/error.hpp>

#include <android/asset_manager.h>

#include <cstddef>
#include <expected>
#include <memory>
#include <span>

namespace vultra
{
    // Read-only mapping of an uncompressed APK asset. The manager is borrowed only while opening it.
    class AndroidAsset
    {
    public:
        [[nodiscard]] static std::expected<std::unique_ptr<AndroidAsset>, Error> open(AAssetManager& manager,
                                                                                      const char*    name);
        ~AndroidAsset();
        AndroidAsset(const AndroidAsset&)            = delete;
        AndroidAsset& operator=(const AndroidAsset&) = delete;

        std::span<const std::byte> bytes() const;

    private:
        AndroidAsset(void* mapping, size_t mappedSize, size_t prefix, size_t size);

        void*  m_Mapping    = nullptr;
        size_t m_MappedSize = 0;
        size_t m_Prefix     = 0;
        size_t m_Size       = 0;
    };
} // namespace vultra
