#pragma once

#include <vultra/drivers/rhi/resources.hpp>

#include <expected>
#include <memory>
#include <string>

namespace vultra
{
    std::expected<std::unique_ptr<Texture>, std::string> createOpenPbrLutsExpected(Device& device);
}
