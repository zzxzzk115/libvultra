#ifndef RAY_DIFFERENTIALS_GLSL
#define RAY_DIFFERENTIALS_GLSL

#include "bda_vertex.glsl"
#include "resources/camera_block.glsl"

struct UVDiff
{
    vec2  duvdx;
    vec2  duvdy;
    float lod;
};

vec3 computeBarycentric(vec3 v0v1, vec3 v0v2, vec3 vp)
{
    float d00    = dot(v0v1, v0v1);
    float d01    = dot(v0v1, v0v2);
    float d11    = dot(v0v2, v0v2);
    float d20    = dot(vp, v0v1);
    float d21    = dot(vp, v0v2);
    float invDen = 1.0 / (d00 * d11 - d01 * d01);

    float b1 = (d11 * d20 - d01 * d21) * invDen;
    float b2 = (d00 * d21 - d01 * d20) * invDen;
    float b0 = 1.0 - b1 - b2;

    return vec3(b0, b1, b2);
}

UVDiff computeUVDiffPrimary(vec3      ro,
                            vec3      rd,
                            vec3      rd_dx,
                            vec3      rd_dy,
                            Vertex    v0,
                            Vertex    v1,
                            Vertex    v2,
                            vec2      attribs,
                            sampler2D tex)
{
    UVDiff d;
    vec3   N = normalize(cross(v1.position - v0.position, v2.position - v0.position));

    float t = dot(v0.position - ro, N) / dot(rd, N);
    vec3  P = ro + t * rd;

    float t_dx = dot(v0.position - ro, N) / dot(rd_dx, N);
    float t_dy = dot(v0.position - ro, N) / dot(rd_dy, N);
    vec3  Pdx  = ro + t_dx * rd_dx;
    vec3  Pdy  = ro + t_dy * rd_dy;

    vec3 v0v1  = v1.position - v0.position;
    vec3 v0v2  = v2.position - v0.position;
    vec3 v0P   = P - v0.position;
    vec3 v0Pdx = Pdx - v0.position;
    vec3 v0Pdy = Pdy - v0.position;

    vec3 bc    = computeBarycentric(v0v1, v0v2, v0P);
    vec3 bc_dx = computeBarycentric(v0v1, v0v2, v0Pdx);
    vec3 bc_dy = computeBarycentric(v0v1, v0v2, v0Pdy);

    vec2 uv0 = v0.texCoord;
    vec2 uv1 = v1.texCoord;
    vec2 uv2 = v2.texCoord;

    vec2 uv   = uv0 * bc.x + uv1 * bc.y + uv2 * bc.z;
    vec2 uv_x = uv0 * bc_dx.x + uv1 * bc_dx.y + uv2 * bc_dx.z;
    vec2 uv_y = uv0 * bc_dy.x + uv1 * bc_dy.y + uv2 * bc_dy.z;

    d.duvdx = uv_x - uv;
    d.duvdy = uv_y - uv;

    // --- perspective correction ---
    vec3  cameraPos        = u_Camera.inversedView[3].xyz;
    float z                = max(dot(N, P - cameraPos), 1e-4);
    float perspectiveScale = u_Camera.near / (z * tan(u_Camera.fovY * 0.5));
    perspectiveScale       = clamp(perspectiveScale, 0.05, 1.0); // soft clamp to stabilize

    d.duvdx *= perspectiveScale;
    d.duvdy *= perspectiveScale;
    // -------------------------------

    vec2  texDim = vec2(textureSize(tex, 0));
    vec2  dsdx   = d.duvdx * texDim;
    vec2  dsdy   = d.duvdy * texDim;
    float rho2   = max(dot(dsdx, dsdx), dot(dsdy, dsdy));
    d.lod        = max(0.0, 0.5 * log2(rho2));

    return d;
}

#endif // RAY_DIFFERENTIALS_GLSL