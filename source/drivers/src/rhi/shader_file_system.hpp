#pragma once

#include <vultra/drivers/rhi/shader_program.hpp>

#include <slang-com-ptr.h>
#include <slang.h>
#include <xxhash.h>

#include <atomic>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>

namespace vultra::detail
{
    // A compilation sees one immutable copy of every file. Hash the bytes Slang used, not a later disk read.
    class ShaderFileSystem final : public ISlangFileSystem
    {
    public:
        SlangResult SLANG_MCALL queryInterface(const SlangUUID& uuid, void** output) noexcept override
        {
            *output = castAs(uuid);
            if (!*output)
            {
                return SLANG_E_NO_INTERFACE;
            }
            addRef();
            return SLANG_OK;
        }

        void* SLANG_MCALL castAs(const SlangUUID& uuid) noexcept override
        {
            for (const auto& supported :
                 {ISlangUnknown::getTypeGuid(), ISlangCastable::getTypeGuid(), ISlangFileSystem::getTypeGuid()})
            {
                if (std::memcmp(&uuid, &supported, sizeof(uuid)) == 0)
                {
                    return static_cast<ISlangFileSystem*>(this);
                }
            }
            return nullptr;
        }

        uint32_t SLANG_MCALL addRef() noexcept override
        {
            return ++m_References;
        }

        uint32_t SLANG_MCALL release() noexcept override
        {
            const auto remaining = --m_References;
            if (!remaining)
            {
                delete this;
            }
            return remaining;
        }

        SlangResult SLANG_MCALL loadFile(const char* path, ISlangBlob** output) noexcept override
        {
            *output = nullptr;
            try
            {
                const std::scoped_lock lock(m_Mutex);
                const auto             file  = std::filesystem::weakly_canonical(std::filesystem::absolute(path));
                auto                   found = m_Files.find(file);
                if (found == m_Files.end())
                {
                    if (!std::filesystem::is_regular_file(file))
                    {
                        return SLANG_E_NOT_FOUND;
                    }
                    if (std::filesystem::file_size(file) > 64 * 1024 * 1024)
                    {
                        return SLANG_FAIL;
                    }
                    std::ifstream input(file, std::ios::binary);
                    if (!input)
                    {
                        return SLANG_FAIL;
                    }
                    const std::string text {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
                    if (!input.eof() && !input.good())
                    {
                        return SLANG_FAIL;
                    }
                    Slang::ComPtr<ISlangBlob> blob;
                    blob.attach(slang_createBlob(text.data(), text.size()));
                    if (!blob)
                    {
                        return SLANG_FAIL;
                    }
                    found = m_Files.emplace(file, std::move(blob)).first;
                }
                *output = found->second.get();
                (*output)->addRef();
                return SLANG_OK;
            }
            catch (...)
            {
                return SLANG_FAIL;
            }
        }

        std::vector<ShaderDependency> dependencies() const
        {
            const std::scoped_lock        lock(m_Mutex);
            std::vector<ShaderDependency> result;
            for (const auto& [path, blob] : m_Files)
            {
                result.push_back({path, XXH3_64bits(blob->getBufferPointer(), blob->getBufferSize())});
            }
            return result;
        }

    private:
        std::atomic<uint32_t>                                      m_References {1};
        mutable std::mutex                                         m_Mutex;
        std::map<std::filesystem::path, Slang::ComPtr<ISlangBlob>> m_Files;
    };
} // namespace vultra::detail
