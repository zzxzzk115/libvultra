#include <vultra/platform/android/asset.hpp>

#include <cerrno>
#include <cstdint>
#include <limits>
#include <string>
#include <sys/mman.h>
#include <system_error>
#include <unistd.h>

namespace vultra
{
    AndroidAsset::AndroidAsset(void* mapping, size_t mappedSize, size_t prefix, size_t size) :
        m_Mapping(mapping),
        m_MappedSize(mappedSize),
        m_Prefix(prefix),
        m_Size(size)
    {
    }

    std::expected<std::unique_ptr<AndroidAsset>, Error> AndroidAsset::open(AAssetManager& manager, const char* name)
    {
        if (!name || !*name)
        {
            return makeError("APK asset name is empty");
        }
        std::unique_ptr<AAsset, decltype(&AAsset_close)> asset(AAssetManager_open(&manager, name, AASSET_MODE_RANDOM),
                                                               &AAsset_close);
        if (!asset)
        {
            return makeError("Open APK asset: {}", name);
        }
        off64_t   offset     = 0;
        off64_t   length     = 0;
        const int descriptor = AAsset_openFileDescriptor64(asset.get(), &offset, &length);
        if (descriptor < 0)
        {
            return makeError("Map APK asset: store '{}' uncompressed (androidResources.noCompress += 'vpk')", name);
        }
        const auto pageSize = sysconf(_SC_PAGESIZE);
        if (pageSize <= 0 || offset < 0 || length <= 0 ||
            uint64_t(length) > std::numeric_limits<size_t>::max() - uint64_t(pageSize))
        {
            close(descriptor);
            return makeError("Map APK asset: invalid bounds for {}", name);
        }
        const auto prefix     = size_t(offset % pageSize);
        const auto size       = size_t(length);
        const auto mappedSize = prefix + size;
        auto*      mapping    = mmap(nullptr, mappedSize, PROT_READ, MAP_PRIVATE, descriptor, offset - off64_t(prefix));
        const int  mappingError = errno;
        close(descriptor);
        if (mapping == MAP_FAILED)
        {
            return makeError("Map APK asset: {}: {}",
                             name,
                             std::error_code(mappingError, std::generic_category()).message());
        }
        return std::unique_ptr<AndroidAsset>(new AndroidAsset(mapping, mappedSize, prefix, size));
    }

    AndroidAsset::~AndroidAsset()
    {
        munmap(m_Mapping, m_MappedSize);
    }

    std::span<const std::byte> AndroidAsset::bytes() const
    {
        return {static_cast<const std::byte*>(m_Mapping) + m_Prefix, m_Size};
    }
} // namespace vultra
