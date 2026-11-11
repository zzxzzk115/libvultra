#pragma once

#include <vultra/gpu/resources.hpp>

#include <memory>

namespace vultra
{
    std::unique_ptr<Texture> createOpenPbrLuts(Device& device);
}
