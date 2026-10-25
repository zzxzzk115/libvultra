#pragma once

#include <vultra/drivers/rhi/shader_program.hpp>
#include <vultra/servers/rendering/graph/pass_catalog.hpp>

namespace research
{
    // A supplied program is borrowed until the catalog has constructed its passes.
    vultra::PassDefinition colorGainDefinition(const vultra::ShaderProgram* program = nullptr);
} // namespace research
