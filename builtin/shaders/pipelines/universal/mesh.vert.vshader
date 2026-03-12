[vshader]
language = glsl
version = 460

[keywords]
VTX_HAS_COLOR : bool permute
VTX_HAS_NORMAL : bool permute
VTX_HAS_UV0 : bool permute
VTX_HAS_UV1 : bool permute
VTX_HAS_TANGENT : bool permute

[vert]
#include "include/common/gpu_scene.glsl"

// ============================================================================
// mesh.vert.vshader
//
// Built-in GPU-driven mesh vertex shader.
//
// - Multi-draw indirect: gl_DrawID selects DrawRecord
// - Vertex pulling: buffer device address for vertex/index buffers
// - Legacy-compatible TBN construction (Gram-Schmidt + handedness)
//
// ============================================================================

#if VTX_HAS_COLOR
layout(location = 0) out vec3 v_Color;
#endif

#if VTX_HAS_UV0
layout(location = 1) out vec2 v_TexCoord0;
#endif

#if VTX_HAS_UV1
layout(location = 2) out vec2 v_TexCoord1;
#endif

layout(location = 3) out vec3 v_FragPos;

#if VTX_HAS_TANGENT && VTX_HAS_NORMAL
layout(location = 4) out mat3 v_TBN;
#elif VTX_HAS_NORMAL
layout(location = 4) out vec3 v_Normal;
#endif

layout(location = 7) flat out uint v_MaterialIndex;

layout(location = 8) out vec4 v_Debug;

void main()
{
    DrawRecord d = s_Draws.draws[gl_InstanceIndex];

    v_Debug = d.model[1]; // Debug: visualize model matrix first column

    VertexBuffer vb = VertexBuffer(d.vertexAddress);

    // Indexed, gl_VertexIndex is the final vertex index that we want to pull.
    Vertex v   = vb.vertices[gl_VertexIndex];

#if VTX_HAS_COLOR
    v_Color    = v.color;
#endif

#if VTX_HAS_UV0
    v_TexCoord0 = v.texCoord0;
#endif

#if VTX_HAS_UV1
	v_TexCoord1 = v.texCoord1;
#endif

    vec4 worldPos4 = d.model * vec4(v.position, 1.0);
    v_FragPos      = worldPos4.xyz;

    // Legacy-compatible normal matrix
    mat3 normalMatrix = transpose(inverse(mat3(d.model)));

#if VTX_HAS_TANGENT && VTX_HAS_NORMAL
    vec3 T = normalize(normalMatrix * v.tangent.xyz);
    vec3 N = normalize(normalMatrix * v.normal);

    // Gram-Schmidt orthogonalize
    T = normalize(T - dot(T, N) * N);

    vec3 B = cross(N, T) * v.tangent.w;

    v_TBN = mat3(T, B, N);
#elif VTX_HAS_NORMAL
    v_Normal = normalize(normalMatrix * v.normal);
#endif

    v_MaterialIndex = d.materialIndex;

    gl_Position = u_Camera.viewProjection * vec4(v_FragPos, 1.0);
}
