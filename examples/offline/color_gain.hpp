#pragma once

#include <vultra/graph/pass_catalog.hpp>
#include <vultra/shader/shader_program.hpp>

namespace research
{
    // A supplied program is borrowed until the catalog has constructed its passes.
    vultra::PassDefinition colorGainDefinition(const vultra::ShaderProgram* program = nullptr);
} // namespace research
