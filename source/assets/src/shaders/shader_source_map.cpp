#include <vultra/assets/shader_asset.hpp>

#include <algorithm>
#include <stdexcept>

namespace vultra
{
    std::expected<void, std::string> ShaderSourceProjection::mapBlockExpected(const ShaderSourceBlock& block,
                                                                              size_t&                  cursor)
    {
        if (block.text.empty())
        {
            return {};
        }
        const auto position = text.find(block.text, cursor);
        if (position == std::string::npos)
        {
            return std::unexpected("Generated shader lost its original Slang block");
        }
        const auto line  = uint32_t(std::count(text.begin(), text.begin() + position, '\n')) + 1;
        const auto lines = uint32_t(std::count(block.text.begin(), block.text.end(), '\n')) + 1;
        mappings.push_back({line, lines, block.location});
        cursor = position + block.text.size();
        return {};
    }

    void ShaderSourceProjection::mapBlock(const ShaderSourceBlock& block, size_t& cursor)
    {
        if (auto mapped = mapBlockExpected(block, cursor); !mapped)
        {
            throw std::logic_error(mapped.error());
        }
    }
} // namespace vultra
