#include "passes/forward.slang"

uint4 meshletDrawParameters() { return material.meshlets; }
uint meshletInstanceIndex() { return material.instance.x; }
SurfaceVertex meshletSurfaceVertex(MeshVertex input) { return transformSurfaceVertex(input); }
#define VULTRA_TASK_ENTRY taskMain
#define VULTRA_MESH_ENTRY meshMain
#include "passes/meshlet_entries.slangh"
