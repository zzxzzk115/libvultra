#include <vultra/platform/android/asset.hpp>

#include <cerrno>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <system_error>
#include <unistd.h>

namespace vultra
{
    AndroidAsset::AndroidAsset(AAssetManager& manager, const char* name)
    {
        if (!name || !*name)
        {
            throw std::invalid_argument("APK asset name is empty");
        }
        std::unique_ptr<AAsset, decltype(&AAsset_close)> asset(AAssetManager_open(&manager, name, AASSET_MODE_RANDOM),
                                                               &AAsset_close);
        if (!asset)
        {
            throw std::runtime_error(std::string("Open APK asset: ") + name);
        }
        off64_t   offset     = 0;
        off64_t   length     = 0;
        const int descriptor = AAsset_openFileDescriptor64(asset.get(), &offset, &length);
        if (descriptor < 0)
        {
            throw std::runtime_error(std::string("Map APK asset: store '") + name +
                                     "' uncompressed (androidResources.noCompress += 'vpk')");
        }
        const auto pageSize = sysconf(_SC_PAGESIZE);
        if (pageSize <= 0 || offset < 0 || length <= 0 ||
            uint64_t(length) > std::numeric_limits<size_t>::max() - uint64_t(pageSize))
        {
            close(descriptor);
            throw std::runtime_error(std::string("Map APK asset: invalid bounds for ") + name);
        }
        m_Prefix     = size_t(offset % pageSize);
        m_Size       = size_t(length);
        m_MappedSize = m_Prefix + m_Size;
        m_Mapping    = mmap(nullptr, m_MappedSize, PROT_READ, MAP_PRIVATE, descriptor, offset - off64_t(m_Prefix));
        const int mappingError = errno;
        close(descriptor);
        if (m_Mapping == MAP_FAILED)
        {
            throw std::system_error(mappingError, std::generic_category(), std::string("Map APK asset: ") + name);
        }
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
