#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace vultra
{
    // Persistent UUID. Runtime ObjectId and RenderingServer RIDs are separate identities.
    struct StableId
    {
        std::array<uint8_t, 16> bytes {};

        static StableId                generate();
        static std::optional<StableId> parse(std::string_view text);
        std::string                    toString() const;
        bool                           valid() const;

        friend bool operator==(const StableId&, const StableId&) = default;
    };

    struct ObjectId
    {
        uint64_t    value                          = 0;
        friend bool operator==(ObjectId, ObjectId) = default;
    };
} // namespace vultra
