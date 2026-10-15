#pragma once

#include <vultra/drivers/rhi/resources.hpp>

#include <memory>

namespace vultra
{
    std::unique_ptr<Texture> createOpenPbrLuts(Device& device);
}
