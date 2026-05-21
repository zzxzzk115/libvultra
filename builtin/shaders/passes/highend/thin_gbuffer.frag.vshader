[vshader]
language = glsl
version = 460

[frag]
#define VTX_HAS_COLOR 0
#define VTX_HAS_NORMAL 1
#define VTX_HAS_UV0 1
#define VTX_HAS_UV1 0
#define VTX_HAS_TANGENT 1
#define VULTRA_DECLARE_CAMERA
#define VULTRA_DECLARE_DRAW_BUFFER_READONLY
#define VULTRA_DECLARE_MESHLET_BUFFER
#define VULTRA_DECLARE_MATERIAL_TABLE
#define VULTRA_DECLARE_MATERIAL_PARAMS
#define VULTRA_DECLARE_MESHLET_VERTEX_BUFFER
#define VULTRA_DECLARE_MESHLET_TRIANGLE_BUFFER
#define VULTRA_DECLARE_BINDLESS_TEXTURES
#include "include/common/gpu_scene.glsl"
#include "include/common/bda_vertex.glsl"

layout(set = 3, binding = 1) uniform usampler2D u_VisibilityBuffer;

layout(location = 0) in vec2 v_TexCoord;

layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 GBufferNormal;
layout(location = 2) out vec4 GBufferMetallicRoughnessAO;

const uint VULTRA_VISIBILITY_INVALID = 0xFFFFFFFFu;

vec3 perspective_divide(vec4 clip)
{
    vec3 ndc = clip.xyz / max(abs(clip.w), 1e-6);
    return vec3((ndc.xy * 0.5 + 0.5) * u_Camera.resolution.xy, ndc.z);
}

vec3 barycentric(vec2 p, vec2 a, vec2 b, vec2 c)
{
    vec2 v0 = b - a;
    vec2 v1 = c - a;
    vec2 v2 = p - a;
    float d00 = dot(v0, v0);
    float d01 = dot(v0, v1);
    float d11 = dot(v1, v1);
    float d20 = dot(v2, v0);
    float d21 = dot(v2, v1);
    float denom = d00 * d11 - d01 * d01;
    if (abs(denom) <= 1e-8)
        return vec3(1.0, 0.0, 0.0);

    float v = (d11 * d20 - d01 * d21) / denom;
    float w = (d00 * d21 - d01 * d20) / denom;
    return vec3(1.0 - v - w, v, w);
}

vec3 perspective_correct_barycentric(vec3 screenBarycentric, vec4 clip0, vec4 clip1, vec4 clip2)
{
    vec3 invW = 1.0 / vec3(clip0.w, clip1.w, clip2.w);
    vec3 weighted = screenBarycentric * invW;
    float sumWeights = weighted.x + weighted.y + weighted.z;
    if (abs(sumWeights) <= 1e-8)
        return screenBarycentric;
    return weighted / sumWeights;
}

vec4 base_color_for_material(uint materialIndex, vec2 uv)
{
    uint model = get_material_model(materialIndex);
    if (model == VULTRA_MAT_PBRMR)
    {
        MaterialParamsPBRMR params = get_pbrmr_params(materialIndex);
        vec4 color = params.baseColor;
        if (params.baseColorTex != 0u)
            color *= texture(getBindlessTexture(params.baseColorTex), uv);
        return color;
    }
    if (model == VULTRA_MAT_PBRSG)
    {
        MaterialParamsPBRSG params = get_pbrsg_params(materialIndex);
        vec4 color = params.diffuseColor;
        if (params.diffuseColorTex != 0u)
            color *= texture(getBindlessTexture(params.diffuseColorTex), uv);
        return color;
    }
    if (model == VULTRA_MAT_UNLIT)
    {
        MaterialParamsUnlit params = get_unlit_params(materialIndex);
        vec4 color = params.color;
        if (params.colorTex != 0u)
            color *= texture(getBindlessTexture(params.colorTex), uv);
        return color;
    }
    if (model == VULTRA_MAT_PHONG)
    {
        MaterialParamsPhong params = get_phong_params(materialIndex);
        vec4 color = params.diffuse;
        if (params.diffuseTex != 0u)
            color *= texture(getBindlessTexture(params.diffuseTex), uv);
        return color;
    }
    return vec4(1.0);
}

vec3 material_mra(uint materialIndex, vec2 uv)
{
    uint model = get_material_model(materialIndex);
    if (model == VULTRA_MAT_PBRMR)
    {
        MaterialParamsPBRMR params = get_pbrmr_params(materialIndex);
        float metallic = params.metallicFactor;
        float roughness = params.roughnessFactor;
        float ao = 1.0;
        if (params.mrTex != 0u)
        {
            vec4 mr = texture(getBindlessTexture(params.mrTex), uv);
            metallic *= mr.b;
            roughness *= mr.g;
        }
        if (params.occlusionTex != 0u)
            ao *= texture(getBindlessTexture(params.occlusionTex), uv).r;
        return vec3(metallic, roughness, ao);
    }
    if (model == VULTRA_MAT_PBRSG)
    {
        MaterialParamsPBRSG params = get_pbrsg_params(materialIndex);
        return vec3(0.0, clamp(1.0 - params.glossinessFactor, 0.02, 1.0), 1.0);
    }
    if (model == VULTRA_MAT_PHONG)
    {
        MaterialParamsPhong params = get_phong_params(materialIndex);
        float roughness = clamp(1.0 / sqrt(max(params.specularShininess.w, 1.0)), 0.02, 1.0);
        return vec3(0.0, roughness, 1.0);
    }
    return vec3(0.0, 1.0, 1.0);
}

void main()
{
    uvec4 visSample = texelFetch(u_VisibilityBuffer, ivec2(gl_FragCoord.xy), 0);
    uint visibility = visSample.r;
    if (visibility == VULTRA_VISIBILITY_INVALID)
    {
        discard;
    }

    uint drawId = visibility >> 16;
    uint triIndex = visibility & 0xFFFFu;

    DrawRecord d = s_Draws.draws[drawId];
    Meshlet meshlet = s_Meshlets.meshlets[d.primitiveIndex];

    uint triBase = meshlet.triangleOffset + triIndex * 3u;
    uint local0 = load_meshlet_triangle_index(triBase + 0u);
    uint local1 = load_meshlet_triangle_index(triBase + 1u);
    uint local2 = load_meshlet_triangle_index(triBase + 2u);
    uint vertex0 = s_MeshletVertices.meshletVertices[meshlet.vertexOffset + local0];
    uint vertex1 = s_MeshletVertices.meshletVertices[meshlet.vertexOffset + local1];
    uint vertex2 = s_MeshletVertices.meshletVertices[meshlet.vertexOffset + local2];

    VertexBuffer vb = VertexBuffer(d.vertexAddress);
    Vertex v0 = vb.vertices[vertex0];
    Vertex v1 = vb.vertices[vertex1];
    Vertex v2 = vb.vertices[vertex2];

    vec4 w0 = d.model * vec4(v0.position, 1.0);
    vec4 w1 = d.model * vec4(v1.position, 1.0);
    vec4 w2 = d.model * vec4(v2.position, 1.0);

    vec4 c0 = u_Camera.viewProjection * w0;
    vec4 c1 = u_Camera.viewProjection * w1;
    vec4 c2 = u_Camera.viewProjection * w2;

    vec3 s0 = perspective_divide(c0);
    vec3 s1 = perspective_divide(c1);
    vec3 s2 = perspective_divide(c2);
    vec3 screenBc = barycentric(gl_FragCoord.xy, s0.xy, s1.xy, s2.xy);
    vec3 bc = perspective_correct_barycentric(screenBc, c0, c1, c2);

    vec2 uv = vtx_uv0(v0) * bc.x + vtx_uv0(v1) * bc.y + vtx_uv0(v2) * bc.z;

    mat3 normalMatrix = transpose(inverse(mat3(d.model)));
    vec3 n0 = normalize(normalMatrix * vtx_normal(v0));
    vec3 n1 = normalize(normalMatrix * vtx_normal(v1));
    vec3 n2 = normalize(normalMatrix * vtx_normal(v2));
    vec3 normalWS = normalize(n0 * bc.x + n1 * bc.y + n2 * bc.z);

    vec4 color = base_color_for_material(d.materialIndex, uv);
    vec3 mra = material_mra(d.materialIndex, uv);

    FragColor = color;
    GBufferNormal = vec4(normalWS, 1.0);
    GBufferMetallicRoughnessAO = vec4(mra, 1.0);
}
