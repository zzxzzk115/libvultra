#pragma once

#include <android/asset_manager.h>

#include <cstddef>
#include <span>

namespace vultra
{
    // Read-only mapping of an uncompressed APK asset. The manager is borrowed only while opening it.
    class AndroidAsset
    {
    public:
        AndroidAsset(AAssetManager& manager, const char* name);
        ~AndroidAsset();
        AndroidAsset(const AndroidAsset&)            = delete;
        AndroidAsset& operator=(const AndroidAsset&) = delete;

        std::span<const std::byte> bytes() const;

    private:
        void*  m_Mapping    = nullptr;
        size_t m_MappedSize = 0;
        size_t m_Prefix     = 0;
        size_t m_Size       = 0;
    };
} // namespace vultra
