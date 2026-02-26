#version 460

// ============================================================================
// mesh.vert.vshader
//
// Built-in Mesh Vertex Shader (CPU-driven version)
//
// No instancing
// No skinning
//
// Compatible with:
//
//  libvultra TestMaterialPass
//  vshadersystem
//
// ============================================================================



// ============================================================================
// Keywords
// ============================================================================

// Vertex layout permutation

#pragma keyword permute HAS_NORMAL=0|1
#pragma keyword permute HAS_TANGENT=0|1
#pragma keyword permute HAS_COLOR=0|1
#pragma keyword permute HAS_TEXCOORD0=0|1
#pragma keyword permute HAS_TEXCOORD1=0|1


// Pass type

#pragma keyword permute PASS=TEST_MATERIAL



// ============================================================================
// Camera
// ============================================================================

layout(set = 0, binding = 0) uniform Camera
{
    mat4 viewProj;
} uCamera;



// ============================================================================
// Per-draw transform
// ============================================================================
//
// CPU-driven renderer:
// set once per draw
//

layout(push_constant) uniform PushConstants
{
    mat4 model;
} uPush;




// ============================================================================
// Vertex Input
// ============================================================================

layout(location = 0) in vec3 inPosition;

#if HAS_NORMAL
layout(location = 1) in vec3 inNormal;
#endif

#if HAS_TANGENT
layout(location = 2) in vec4 inTangent;
#endif

#if HAS_COLOR
layout(location = 3) in vec4 inColor;
#endif

#if HAS_TEXCOORD0
layout(location = 4) in vec2 inTexCoord0;
#endif

#if HAS_TEXCOORD1
layout(location = 5) in vec2 inTexCoord1;
#endif




// ============================================================================
// Vertex Output
// ============================================================================

layout(location = 0) out vec3 vWorldPos;

#if HAS_NORMAL
layout(location = 1) out vec3 vNormal;
#endif

#if HAS_COLOR
layout(location = 2) out vec4 vColor;
#endif

#if HAS_TEXCOORD0
layout(location = 3) out vec2 vUV0;
#endif

#if HAS_TEXCOORD1
layout(location = 4) out vec2 vUV1;
#endif




// ============================================================================
// Main
// ============================================================================

void main()
{

    vec4 worldPos =
        uPush.model * vec4(inPosition, 1.0);

    vWorldPos =
        worldPos.xyz;


#if HAS_NORMAL

    vNormal =
        mat3(uPush.model) * inNormal;

#endif


#if HAS_COLOR

    vColor =
        inColor;

#endif


#if HAS_TEXCOORD0

    vUV0 =
        inTexCoord0;

#endif


#if HAS_TEXCOORD1

    vUV1 =
        inTexCoord1;

#endif


    gl_Position =
        uCamera.viewProj * worldPos;

}
