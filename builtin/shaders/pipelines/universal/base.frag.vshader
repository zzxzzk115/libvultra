#version 460

#include "include/common/gpu_scene.glsl"

// ============================================================================
// base.frag.vshader
//
// Built-in GPU-driven base fragment shader.
//
// - Demonstrates material pulling via the index emitted by mesh.vert.vshader
//
// ============================================================================

layout(location = 7) flat in uint v_MaterialIndex;

layout(location = 0) out vec4 FragColor;

void main()
{
    FragColor = material_main_color(v_MaterialIndex);
}
