[vshader]
language = glsl
version = 460

[keywords]
WRITE_ENTITY_ID : bool permute

[frag]
#define VULTRA_DECLARE_CAMERA
#define VULTRA_DECLARE_DRAW_BUFFER_READONLY
#define VULTRA_DECLARE_MESHLET_BUFFER
#define VULTRA_DECLARE_MATERIAL_TABLE
#define VULTRA_DECLARE_MATERIAL_PARAMS
#define VULTRA_DECLARE_MESHLET_VERTEX_BUFFER
#define VULTRA_DECLARE_MESHLET_TRIANGLE_BUFFER
#define VULTRA_DECLARE_SKIN_MATRIX_BUFFER
#define VULTRA_DECLARE_BINDLESS_TEXTURES
#include "include/common/gpu_scene.glsl"
#include "include/common/bda_vertex.glsl"

layout(set = 3, binding = 1) uniform usampler2D u_VisibilityBuffer;

layout(location = 0) in vec2 v_TexCoord;

layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 GBufferNormal;
layout(location = 2) out vec4 GBufferMaterial;
#if WRITE_ENTITY_ID
layout(location = 3) out vec4 GBufferEntityId;
#endif

layout(push_constant) uniform ThinGBufferPushConstants
{
    uint maxDraws;
    uint maxMeshlets;
    uint maxMeshletVertices;
    uint maxMeshletTriangles;
} u_PC;

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

vec4 base_color_for_material(uint materialIndex, vec2 uv, bool hasUv0)
{
    uint model = get_material_model(materialIndex);
    if (model == VULTRA_MAT_PBRMR)
    {
        MaterialParamsPBRMR params = get_pbrmr_params(materialIndex);
        vec4 color = params.baseColor;
        if (hasUv0 && params.baseColorTex != 0u)
            color *= texture(getBindlessTexture(params.baseColorTex), uv);
        return color;
    }
    if (model == VULTRA_MAT_PBRSG)
    {
        MaterialParamsPBRSG params = get_pbrsg_params(materialIndex);
        vec4 color = params.diffuseColor;
        if (hasUv0 && params.diffuseColorTex != 0u)
            color *= texture(getBindlessTexture(params.diffuseColorTex), uv);
        return color;
    }
    if (model == VULTRA_MAT_UNLIT)
    {
        MaterialParamsUnlit params = get_unlit_params(materialIndex);
        vec4 color = params.color;
        if (hasUv0 && params.colorTex != 0u)
            color *= texture(getBindlessTexture(params.colorTex), uv);
        return color;
    }
    if (model == VULTRA_MAT_PHONG)
    {
        MaterialParamsPhong params = get_phong_params(materialIndex);
        vec4 color = params.diffuse;
        if (hasUv0 && params.diffuseTex != 0u)
            color *= texture(getBindlessTexture(params.diffuseTex), uv);
        return color;
    }
    if (model == VULTRA_MAT_GRAPH)
    {
        MaterialParamsGraph params = get_graph_params(materialIndex);
        vec4 color = vec4(params.baseColor.rgb + params.emissiveAlpha.rgb, params.baseColor.a * params.emissiveAlpha.a);
        if (hasUv0 && params.textureInfo.x != 0u)
            color *= texture(getBindlessTexture(params.textureInfo.x), uv);
        return color;
    }
    return vec4(1.0);
}

vec3 material_mra(uint materialIndex, vec2 uv, bool hasUv0)
{
    uint model = get_material_model(materialIndex);
    if (model == VULTRA_MAT_PBRMR)
    {
        MaterialParamsPBRMR params = get_pbrmr_params(materialIndex);
        float metallic = params.metallicFactor;
        float roughness = params.roughnessFactor;
        float ao = 1.0;
        if (hasUv0 && params.mrTex != 0u)
        {
            vec4 mr = texture(getBindlessTexture(params.mrTex), uv);
            metallic *= mr.b;
            roughness *= mr.g;
        }
        if (hasUv0 && params.occlusionTex != 0u)
            ao *= texture(getBindlessTexture(params.occlusionTex), uv).r;
        return vec3(metallic, roughness, ao);
    }
    if (model == VULTRA_MAT_PBRSG)
    {
        MaterialParamsPBRSG params = get_pbrsg_params(materialIndex);
        float specularIntensity = dot(params.specularFactor, vec3(0.2126, 0.7152, 0.0722));
        return vec3(clamp(specularIntensity, 0.0, 1.0), clamp(1.0 - params.glossinessFactor, 0.02, 1.0), 1.0);
    }
    if (model == VULTRA_MAT_PHONG)
    {
        MaterialParamsPhong params = get_phong_params(materialIndex);
        float roughness = clamp(1.0 / sqrt(max(params.specularShininess.w, 1.0)), 0.02, 1.0);
        float specularIntensity = dot(params.specularShininess.rgb, vec3(0.2126, 0.7152, 0.0722));
        return vec3(clamp(specularIntensity, 0.0, 1.0), roughness, 1.0);
    }
    if (model == VULTRA_MAT_GRAPH)
    {
        MaterialParamsGraph params = get_graph_params(materialIndex);
        if (params.shadingModel == 1u)
            return vec3(0.0, 1.0, params.metallicRoughnessAoCutoff.z);
        return params.metallicRoughnessAoCutoff.xyz;
    }
    return vec3(0.0, 1.0, 1.0);
}

float material_lighting_model(uint materialIndex)
{
    uint model = get_material_model(materialIndex);
    if (model == VULTRA_MAT_GRAPH)
    {
        MaterialParamsGraph params = get_graph_params(materialIndex);
        if (params.shadingModel == 1u)
            return float(VULTRA_MAT_UNLIT);
        if (params.shadingModel == 2u)
            return 6.0;
        if (params.shadingModel == 3u)
            return float(VULTRA_MAT_PBRSG);
        if (params.shadingModel == 4u)
            return float(VULTRA_MAT_PHONG);
        return float(VULTRA_MAT_PBRMR);
    }
    return float(model);
}

vec2 encode_gbuffer_normal(vec3 normalWS)
{
    vec3 n = normalize(normalWS);
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    vec2 encoded = n.xy;
    if (n.z < 0.0)
        encoded = (1.0 - abs(encoded.yx)) * sign(encoded.xy);
    return encoded * 0.5 + 0.5;
}

float encode_material_model(float model)
{
    return clamp(model / 255.0, 0.0, 1.0);
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
    if (drawId >= u_PC.maxDraws)
    {
        discard;
    }

    DrawRecord d = s_Draws.draws[drawId];
    if (d.primitiveIndex >= u_PC.maxMeshlets)
    {
        discard;
    }

    Meshlet meshlet = s_Meshlets.meshlets[d.primitiveIndex];
    if (triIndex >= meshlet.triangleCount)
    {
        discard;
    }

    uint triBase = meshlet.triangleOffset + triIndex * 3u;
    if (triBase + 2u >= u_PC.maxMeshletTriangles)
    {
        discard;
    }

    uint local0 = load_meshlet_triangle_index(triBase + 0u);
    uint local1 = load_meshlet_triangle_index(triBase + 1u);
    uint local2 = load_meshlet_triangle_index(triBase + 2u);
    if (local0 >= meshlet.vertexCount || local1 >= meshlet.vertexCount || local2 >= meshlet.vertexCount)
    {
        discard;
    }

    uint meshletVertex0 = meshlet.vertexOffset + local0;
    uint meshletVertex1 = meshlet.vertexOffset + local1;
    uint meshletVertex2 = meshlet.vertexOffset + local2;
    if (meshletVertex0 >= u_PC.maxMeshletVertices || meshletVertex1 >= u_PC.maxMeshletVertices ||
        meshletVertex2 >= u_PC.maxMeshletVertices)
    {
        discard;
    }

    uint vertex0 = s_MeshletVertices.meshletVertices[meshletVertex0];
    uint vertex1 = s_MeshletVertices.meshletVertices[meshletVertex1];
    uint vertex2 = s_MeshletVertices.meshletVertices[meshletVertex2];

    Vertex v0 = load_vertex(d, vertex0);
    Vertex v1 = load_vertex(d, vertex1);
    Vertex v2 = load_vertex(d, vertex2);

    mat4 skin0 = vtx_skin_matrix(d, v0);
    mat4 skin1 = vtx_skin_matrix(d, v1);
    mat4 skin2 = vtx_skin_matrix(d, v2);
    vec4 w0 = d.model * skin0 * vec4(v0.position, 1.0);
    vec4 w1 = d.model * skin1 * vec4(v1.position, 1.0);
    vec4 w2 = d.model * skin2 * vec4(v2.position, 1.0);

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
    vec3 n0 = normalize(normalMatrix * (mat3(skin0) * vtx_normal(v0)));
    vec3 n1 = normalize(normalMatrix * (mat3(skin1) * vtx_normal(v1)));
    vec3 n2 = normalize(normalMatrix * (mat3(skin2) * vtx_normal(v2)));
    vec3 normalWS = normalize(n0 * bc.x + n1 * bc.y + n2 * bc.z);

    bool hasUv0 = vertex_has_attribute(d.vertexAttributeMask, VULTRA_VERTEX_ATTR_UV0);
    vec4 color = base_color_for_material(d.materialIndex, uv, hasUv0);
    vec3 mra = material_mra(d.materialIndex, uv, hasUv0);

    FragColor = color;
    GBufferNormal = vec4(encode_gbuffer_normal(normalWS), 0.0, 1.0);
    GBufferMaterial = vec4(clamp(mra, 0.0, 1.0), encode_material_model(material_lighting_model(d.materialIndex)));
#if WRITE_ENTITY_ID
    uint id = d.entityPickingId & 0x00FFFFFFu;
    GBufferEntityId = vec4(
        float(id & 0xFFu) / 255.0,
        float((id >> 8u) & 0xFFu) / 255.0,
        float((id >> 16u) & 0xFFu) / 255.0,
        1.0);
#endif
}
