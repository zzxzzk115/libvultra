[vshader]
language = glsl
version = 460

[vert]
// Instanced camera-facing billboard. One instance per pool slot; 4 vertices form a triangle-strip
// quad. Dead slots (lifetime <= 0) collapse to a clipped degenerate triangle.
#define VULTRA_DECLARE_CAMERA
#include "include/common/gpu_scene.glsl"

struct GpuParticle
{
    vec4 positionAge;
    vec4 velocityLife;
};

layout(set = 2, binding = 0, std430) readonly buffer ParticleBuffer
{
    GpuParticle particles[];
} s_Particles;

layout(push_constant) uniform ParticlePushConstants
{
    vec4  originAndDt;
    vec4  startVelAndRadius;
    vec4  gravityAndVelVar;
    vec4  lifeAndSizes;      // x lifetime, y lifetimeVariance, z startSize, w endSize
    vec4  startColor;
    vec4  endColor;
    uvec4 counts;
} u_PC;

layout(location = 0) out vec2  v_Offset;
layout(location = 1) out vec4  v_Color;
layout(location = 2) out float v_ViewDepth;
layout(location = 3) out float v_Size;

void main()
{
    GpuParticle p        = s_Particles.particles[gl_InstanceIndex];
    float       lifetime = p.velocityLife.w;
    if (lifetime <= 0.0)
    {
        // Dead slot: emit a degenerate, clipped vertex so the quad produces no fragments.
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        v_Offset    = vec2(0.0);
        v_Color     = vec4(0.0);
        v_ViewDepth = 0.0;
        v_Size      = 0.0;
        return;
    }

    float t     = clamp(p.positionAge.w / lifetime, 0.0, 1.0);
    float size  = mix(u_PC.lifeAndSizes.z, u_PC.lifeAndSizes.w, t);
    vec4  color = mix(u_PC.startColor, u_PC.endColor, t);

    // Triangle-strip quad corners: (-1,-1), (1,-1), (-1,1), (1,1).
    vec2 corner = vec2((gl_VertexIndex & 1) == 0 ? -1.0 : 1.0, gl_VertexIndex < 2 ? -1.0 : 1.0);

    vec3 right    = u_Camera.inverseView[0].xyz;
    vec3 up       = u_Camera.inverseView[1].xyz;
    vec3 worldPos = p.positionAge.xyz + (right * corner.x + up * corner.y) * (size * 0.5);

    vec4 viewPos = u_Camera.view * vec4(worldPos, 1.0);
    v_Offset     = corner;
    v_Color      = color;
    v_ViewDepth  = -viewPos.z;
    v_Size       = size;
    gl_Position  = u_Camera.projection * viewPos;
}

[frag]
// Additive soft sprite with a soft-particle depth fade against the opaque scene depth.
#define VULTRA_DECLARE_CAMERA
#define VULTRA_DECLARE_DEPTH_TEXTURE
#include "include/common/gpu_scene.glsl"

layout(location = 0) in vec2  v_Offset;
layout(location = 1) in vec4  v_Color;
layout(location = 2) in float v_ViewDepth;
layout(location = 3) in float v_Size;

layout(location = 0) out vec4 o_Color;

float linearizeViewDepth(float ndcDepth)
{
    // Vulkan clip-space depth is [0, 1]. Reconstruct view-space distance via the inverse projection.
    vec4 viewPos = u_Camera.inverseProjection * vec4(0.0, 0.0, ndcDepth, 1.0);
    return -(viewPos.z / viewPos.w);
}

void main()
{
    float r2 = dot(v_Offset, v_Offset);
    if (r2 > 1.0)
        discard;

    float falloff = exp(-r2 * 3.0);     // soft circular sprite
    float alpha   = falloff * v_Color.a;

    // Soft-particle fade: attenuate as the particle approaches / passes behind opaque geometry.
    float sceneNdc   = texelFetch(u_DepthTexture, ivec2(gl_FragCoord.xy), 0).r;
    float sceneDepth = linearizeViewDepth(sceneNdc);
    float fade       = clamp((sceneDepth - v_ViewDepth) / max(v_Size, 0.05), 0.0, 1.0);
    alpha *= fade;

    // Premultiplied output; the pipeline blends additively (src=ONE, dst=ONE).
    o_Color = vec4(v_Color.rgb * alpha, alpha);
}
