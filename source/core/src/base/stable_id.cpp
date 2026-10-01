#include <vultra/core/base/stable_id.hpp>

#include <algorithm>
#include <cstddef>
#include <random>

namespace vultra
{
    namespace
    {
        int hexDigit(char value)
        {
            if (value >= '0' && value <= '9')
            {
                return value - '0';
            }
            if (value >= 'a' && value <= 'f')
            {
                return value - 'a' + 10;
            }
            return -1;
        }
    } // namespace

    StableId StableId::generate()
    {
        std::random_device random;
        StableId           id;
        for (size_t i = 0; i < id.bytes.size(); i += 4)
        {
            const uint32_t word = random();
            for (size_t byte = 0; byte < 4; ++byte)
            {
                id.bytes[i + byte] = uint8_t(word >> (byte * 8));
            }
        }
        id.bytes[6] = uint8_t((id.bytes[6] & 0x0f) | 0x40);
        id.bytes[8] = uint8_t((id.bytes[8] & 0x3f) | 0x80);
        return id;
    }

    std::optional<StableId> StableId::parse(std::string_view text)
    {
        if (text.size() != 36 || text[8] != '-' || text[13] != '-' || text[18] != '-' || text[23] != '-')
        {
            return std::nullopt;
        }
        StableId id;
        size_t   byte = 0;
        for (size_t i = 0; i < text.size();)
        {
            if (text[i] == '-')
            {
                ++i;
                continue;
            }
            const int high = hexDigit(text[i++]);
            const int low  = hexDigit(text[i++]);
            if (high < 0 || low < 0)
            {
                return std::nullopt;
            }
            id.bytes[byte++] = uint8_t((high << 4) | low);
        }
        return id.valid() ? std::optional(id) : std::nullopt;
    }

    std::string StableId::toString() const
    {
        constexpr char hex[] = "0123456789abcdef";
        std::string    text;
        text.reserve(36);
        for (size_t i = 0; i < bytes.size(); ++i)
        {
            if (i == 4 || i == 6 || i == 8 || i == 10)
            {
                text += '-';
            }
            text += hex[bytes[i] >> 4];
            text += hex[bytes[i] & 0x0f];
        }
        return text;
    }

    bool StableId::valid() const
    {
        return std::any_of(bytes.begin(),
                           bytes.end(),
                           [](uint8_t byte)
                           {
                               return byte != 0;
                           });
    }
} // namespace vultra
