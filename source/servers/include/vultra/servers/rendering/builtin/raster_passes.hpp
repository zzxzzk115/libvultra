#pragma once

#include <vultra/servers/rendering/builtin/builtin_renderer.hpp>
#include <vultra/servers/rendering/graph/graph_definition.hpp>

namespace vultra
{
    std::vector<PassDefinition> builtinRasterPassDefinitions();
    bool                        usesBuiltinRasterPasses(const GraphDefinition& definition);
    // Renderer and bindings must outlive the catalog, built passes and the graph's last GPU use.
    // One geometry/shadow/skybox stage per renderer view; independent views use independent renderers.
    void bindBuiltinRasterPasses(PassCatalog&              catalog,
                                 BuiltinRenderer&          renderer,
                                 BuiltinRenderer::Outputs& bindings,
                                 Extent                    size);
} // namespace vultra
