#pragma once

#include "vultra/core/base/base.hpp"
#include "vultra/core/base/uuid.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace vultra::material
{
    // Lightweight, future-proof parameter container.
    // Today: name -> raw bytes (plus optional texture UUID binding).
    // Future: vshadersystem reflection packs this into a tightly packed GPU blob.
    class MaterialBlock
    {
    public:
        struct Entry
        {
            std::vector<std::byte> data;
        };

        struct TextureEntry
        {
            CoreUUID textureUUID {};
        };

        template<typename T>
        void set(std::string_view name, const T& value)
        {
            static_assert(std::is_trivially_copyable_v<T>, "MaterialBlock::set requires trivially copyable types");

            Entry e;
            e.data.resize(sizeof(T));
            std::memcpy(e.data.data(), &value, sizeof(T));
            m_Params[std::string(name)] = std::move(e);
        }

        void setRaw(std::string_view name, const std::byte* data, size_t size)
        {
            Entry e;
            e.data.assign(data, data + size);
            m_Params[std::string(name)] = std::move(e);
        }

        void setTexture(std::string_view name, const CoreUUID& uuid)
        {
            m_Textures[std::string(name)] = TextureEntry {uuid};
        }

        const std::unordered_map<std::string, Entry>&        params() const { return m_Params; }
        const std::unordered_map<std::string, TextureEntry>& textures() const { return m_Textures; }

        void clear()
        {
            m_Params.clear();
            m_Textures.clear();
        }

    private:
        std::unordered_map<std::string, Entry>        m_Params;
        std::unordered_map<std::string, TextureEntry> m_Textures;
    };
}
