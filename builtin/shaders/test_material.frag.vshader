#version 460

#include "include/gpu_scene.glsl"

// ============================================================================
// test_material.frag.vshader
//
// Built-in TestMaterialPass fragment shader.
//
// Output policy:
//   - Select a "mainColor" from the material parameter pool.
//   - Route by material model (PBRMR / PBRSG / Unlit / Phong) for validation.
//   - Optional debug view via runtime global DEBUG_VIEW.
//
// ============================================================================

// Keyword routing
#pragma keyword permute PASS=TEST_MATERIAL
#pragma keyword runtime global DEBUG_VIEW=NONE|NORMAL|ALBEDO

// Mark as built-in material shader (enables reflection / state parsing)
#pragma vultra material

layout(location = 0) in vec3 v_Color;
layout(location = 1) in vec2 v_TexCoord;
layout(location = 2) in vec3 v_FragPos;

#if defined(HAS_TANGENT) && HAS_TANGENT
layout(location = 3) in mat3 v_TBN;
#else
layout(location = 3) in vec3 v_Normal;
#endif

layout(location = 6) flat in uint v_MaterialIndex;

layout(location = 0) out vec4 FragColor;

void main()
{
    // DEBUG_VIEW is a runtime global keyword (no variants)
#if defined(DEBUG_VIEW) && (DEBUG_VIEW == NORMAL)
#if defined(HAS_TANGENT) && HAS_TANGENT
    vec3 N = normalize(v_TBN[2]);
#else
    vec3 N = normalize(v_Normal);
#endif
    FragColor = vec4(N * 0.5 + 0.5, 1.0);
    return;
#elif defined(DEBUG_VIEW) && (DEBUG_VIEW == ALBEDO)
    // fallthrough to albedo output
#endif

    MaterialEntry m = s_Materials.materials[v_MaterialIndex];
    vec4 c = material_main_color(v_MaterialIndex);

    // For validation: route by material model.
    // In this pass, all models share the same mainColor convention.
    switch (m.model)
    {
        case VULTRA_MAT_UNLIT:
            FragColor = c;
            break;

        case VULTRA_MAT_PBRMR:
            FragColor = c; // baseColor
            break;

        case VULTRA_MAT_PBRSG:
            FragColor = c; // diffuseColor
            break;

        case VULTRA_MAT_PHONG:
            FragColor = c; // phong diffuse
            break;

        default:
            FragColor = vec4(1.0, 0.0, 1.0, 1.0);
            break;
    }
}
