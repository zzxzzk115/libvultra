#include <vultra/assets/shader_asset.hpp>

#include <algorithm>

namespace vultra
{
    void ShaderSourceProjection::mapBlock(const ShaderSourceBlock& block, size_t& cursor)
    {
        if (block.text.empty())
        {
            return;
        }
        const auto position = text.find(block.text, cursor);
        if (position == std::string::npos)
        {
            throw std::logic_error("Generated shader lost its original Slang block");
        }
        const auto line  = uint32_t(std::count(text.begin(), text.begin() + position, '\n')) + 1;
        const auto lines = uint32_t(std::count(block.text.begin(), block.text.end(), '\n')) + 1;
        mappings.push_back({line, lines, block.location});
        cursor = position + block.text.size();
    }
} // namespace vultra
