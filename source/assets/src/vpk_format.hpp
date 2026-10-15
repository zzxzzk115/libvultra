#pragma once

#include <string>
#include <string_view>

namespace vultra::detail
{
    // One path contract is shared by archive reading/writing and project-file collection.
    std::string canonicalVpkPath(std::string_view text);
} // namespace vultra::detail
