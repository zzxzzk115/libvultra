#pragma once

#include <vultra/core/base/error.hpp>
#include <vultra/gpu/resources.hpp>

#include <expected>
#include <memory>

namespace vultra
{
    [[nodiscard]] std::expected<std::unique_ptr<Texture>, Error> createOpenPbrLuts(Device& device);
}
