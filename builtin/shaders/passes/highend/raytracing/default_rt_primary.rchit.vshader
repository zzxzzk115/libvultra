[vshader]
language = glsl
version = 460

[rchit]
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_GOOGLE_include_directive : require

#define VULTRA_SCENE_SET 2
#define VULTRA_MATERIAL_TABLE_BINDING 1
#define VULTRA_MATERIAL_PARAMS_BINDING 4
#define VULTRA_TEXTURE_SET 3
#define VULTRA_BINDLESS_TEXTURES_BINDING 4
#define VULTRA_DECLARE_MATERIAL_TABLE
#define VULTRA_DECLARE_MATERIAL_PARAMS
#define VULTRA_DECLARE_BINDLESS_TEXTURES
#include "include/common/gpu_scene.glsl"

layout(set = 1, binding = 0, std140) uniform Camera
{
    CameraData data;
} u_CameraBlock;

struct RtDirectionalLight
{
    vec4 directionShadowStrength;
    vec4 colorIntensity;
};

struct RtPointLight
{
    vec4 posIntensity;
    vec4 colorRadius;
};

layout(set = 1, binding = 1, std140) uniform RtLights
{
    uvec4 counts;
    RtDirectionalLight directional;
    RtPointLight pointLights[32];
} u_RtLights;

layout(set = 3, binding = 0) uniform accelerationStructureEXT topLevelAS;

struct GPUInstanceData {
    uint geometryOffset;
    uint geometryCount;
    uint materialOffset;
    uint materialCount;
};
layout(std430, set = 2, binding = 0) readonly buffer InstanceData { GPUInstanceData instances[]; };

struct GPUGeometryNode {
    uint64_t vertexBufferAddress;
    uint64_t indexBufferAddress;
    uint vertexOffset;
    uint materialIndex;
    uint vertexStrideBytes;
    uint positionOffsetBytes;
    uint normalOffsetBytes;
    uint texCoord0OffsetBytes;
    uint tangentOffsetBytes;
};
layout(std430, set = 2, binding = 2) readonly buffer GeometryNodes { GPUGeometryNode geometryNodes[]; };

layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer IndexBuffer { uint indices[]; };
layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer FloatBuffer { float values[]; };

struct HitValue {
    vec3 color;
    vec3 ddx;
    vec3 ddy;
};
layout(location = 0) rayPayloadInEXT HitValue hitValue;
layout(location = 1) rayPayloadEXT bool shadowed;
hitAttributeEXT vec2 attribs;

const uint INVALID_OFFSET = 0xFFFFFFFFu;
const uint ALPHA_MODE_MASK = 1u;

vec3 loadVec3(uint64_t baseAddress, uint vertexIndex, uint strideBytes, uint offsetBytes, vec3 fallback)
{
    if (offsetBytes == INVALID_OFFSET)
        return fallback;

    FloatBuffer data = FloatBuffer(baseAddress + uint64_t(vertexIndex * strideBytes + offsetBytes));
    return vec3(data.values[0], data.values[1], data.values[2]);
}

vec2 loadVec2(uint64_t baseAddress, uint vertexIndex, uint strideBytes, uint offsetBytes, vec2 fallback)
{
    if (offsetBytes == INVALID_OFFSET)
        return fallback;

    FloatBuffer data = FloatBuffer(baseAddress + uint64_t(vertexIndex * strideBytes + offsetBytes));
    return vec2(data.values[0], data.values[1]);
}

vec4 loadVec4(uint64_t baseAddress, uint vertexIndex, uint strideBytes, uint offsetBytes, vec4 fallback)
{
    if (offsetBytes == INVALID_OFFSET)
        return fallback;

    FloatBuffer data = FloatBuffer(baseAddress + uint64_t(vertexIndex * strideBytes + offsetBytes));
    return vec4(data.values[0], data.values[1], data.values[2], data.values[3]);
}

float rcp(float v) { return 1.0 / v; }
vec3 rcp(vec3 v) { return vec3(1.0 / v.x, 1.0 / v.y, 1.0 / v.z); }
vec3 getRow0(mat3x2 m) { return vec3(m[0][0], m[1][0], m[2][0]); }
vec3 getRow1(mat3x2 m) { return vec3(m[0][1], m[1][1], m[2][1]); }

struct BarycentricDeriv
{
    vec3 lambda;
    vec3 ddx;
    vec3 ddy;
};

struct UvGrad
{
    vec2 uv;
    vec2 dx;
    vec2 dy;
};

BarycentricDeriv calcFullBary(vec4 pt0, vec4 pt1, vec4 pt2, vec2 pixelNdc, vec2 twoOverWindowSize)
{
    BarycentricDeriv ret;
    vec3 invW = rcp(vec3(pt0.w, pt1.w, pt2.w));
    vec2 ndc0 = pt0.xy * invW.x;
    vec2 ndc1 = pt1.xy * invW.y;
    vec2 ndc2 = pt2.xy * invW.z;

    float invDet = rcp(determinant(mat2(ndc2 - ndc1, ndc0 - ndc1)));
    ret.ddx = vec3(ndc1.y - ndc2.y, ndc2.y - ndc0.y, ndc0.y - ndc1.y) * invDet * invW;
    ret.ddy = vec3(ndc2.x - ndc1.x, ndc0.x - ndc2.x, ndc1.x - ndc0.x) * invDet * invW;

    float ddxSum = dot(ret.ddx, vec3(1.0));
    float ddySum = dot(ret.ddy, vec3(1.0));
    vec2 delta = pixelNdc - ndc0;
    float interpInvW = invW.x + delta.x * ddxSum + delta.y * ddySum;
    float interpW = rcp(interpInvW);

    ret.lambda.x = interpW * (invW.x + delta.x * ret.ddx.x + delta.y * ret.ddy.x);
    ret.lambda.y = interpW * (0.0 + delta.x * ret.ddx.y + delta.y * ret.ddy.y);
    ret.lambda.z = interpW * (0.0 + delta.x * ret.ddx.z + delta.y * ret.ddy.z);

    ret.ddx *= twoOverWindowSize.x;
    ret.ddy *= twoOverWindowSize.y;
    ddxSum *= twoOverWindowSize.x;
    ddySum *= twoOverWindowSize.y;

    ret.ddy *= -1.0;
    ddySum *= -1.0;

    float interpWddx = rcp(interpInvW + ddxSum);
    float interpWddy = rcp(interpInvW + ddySum);
    ret.ddx = interpWddx * (ret.lambda * interpInvW + ret.ddx) - ret.lambda;
    ret.ddy = interpWddy * (ret.lambda * interpInvW + ret.ddy) - ret.lambda;
    return ret;
}

UvGrad calcUvGrad(vec3 p0, vec3 p1, vec3 p2, vec2 uv0, vec2 uv1, vec2 uv2)
{
    vec2 pixelNdc = (vec2(gl_LaunchIDEXT.xy) + vec2(0.5)) / vec2(gl_LaunchSizeEXT.xy) * 2.0 - 1.0;
    vec2 twoOverWindowSize = 2.0 / u_CameraBlock.data.resolution.xy;
    BarycentricDeriv deriv = calcFullBary(u_CameraBlock.data.viewProjection * vec4(p0, 1.0),
                                          u_CameraBlock.data.viewProjection * vec4(p1, 1.0),
                                          u_CameraBlock.data.viewProjection * vec4(p2, 1.0),
                                          pixelNdc,
                                          twoOverWindowSize);
    mat3x2 uvs = mat3x2(uv0, uv1, uv2);
    vec3 row0 = getRow0(uvs);
    vec3 row1 = getRow1(uvs);

    UvGrad outGrad;
    outGrad.uv = vec2(dot(row0, deriv.lambda), dot(row1, deriv.lambda));
    outGrad.dx = vec2(dot(row0, deriv.ddx), dot(row1, deriv.ddx));
    outGrad.dy = vec2(dot(row0, deriv.ddy), dot(row1, deriv.ddy));
    return outGrad;
}

vec4 sampleTextureGrad(uint tex, vec2 uv, vec2 duvdx, vec2 duvdy, vec4 fallback)
{
    return tex != 0u ? textureGrad(getBindlessTexture(tex), uv, duvdx, duvdy) : fallback;
}

struct RtMaterialSample
{
    vec4 baseColor;
    vec3 normalWS;
    vec3 mra;
    bool unlit;
};

RtMaterialSample sampleMaterial(uint materialIndex, vec2 uv, vec2 duvdx, vec2 duvdy, vec3 normalWS, vec4 tangentWS)
{
    RtMaterialSample outSample;
    outSample.baseColor = vec4(1.0);
    outSample.normalWS = normalWS;
    outSample.mra = vec3(0.0, 1.0, 1.0);
    outSample.unlit = false;

    uint model = get_material_model(materialIndex);
    if (model == VULTRA_MAT_PBRMR)
    {
        MaterialParamsPBRMR p = get_pbrmr_params(materialIndex);
        outSample.baseColor = p.baseColor * sampleTextureGrad(p.baseColorTex, uv, duvdx, duvdy, vec4(1.0));
        outSample.mra = vec3(p.metallicFactor, p.roughnessFactor, 1.0);
        if (p.mrTex != 0u)
        {
            vec4 mr = textureGrad(getBindlessTexture(p.mrTex), uv, duvdx, duvdy);
            outSample.mra.x *= mr.b;
            outSample.mra.y *= mr.g;
        }
        else
        {
            if (p.metallicTex != 0u)
                outSample.mra.x *= textureGrad(getBindlessTexture(p.metallicTex), uv, duvdx, duvdy).r;
            if (p.roughnessTex != 0u)
                outSample.mra.y *= textureGrad(getBindlessTexture(p.roughnessTex), uv, duvdx, duvdy).r;
        }
        if (p.occlusionTex != 0u)
            outSample.mra.z *= textureGrad(getBindlessTexture(p.occlusionTex), uv, duvdx, duvdy).r;
        if (p.normalTex != 0u && tangentWS.w != 0.0)
        {
            vec3 tangent = normalize(tangentWS.xyz - normalWS * dot(normalWS, tangentWS.xyz));
            vec3 bitangent = normalize(cross(normalWS, tangent)) * tangentWS.w;
            vec3 normalTS = textureGrad(getBindlessTexture(p.normalTex), uv, duvdx, duvdy).xyz * 2.0 - 1.0;
            outSample.normalWS = normalize(mat3(tangent, bitangent, normalWS) * normalTS);
        }
    }
    else if (model == VULTRA_MAT_PBRSG)
    {
        MaterialParamsPBRSG p = get_pbrsg_params(materialIndex);
        outSample.baseColor = p.diffuseColor * sampleTextureGrad(p.diffuseColorTex, uv, duvdx, duvdy, vec4(1.0));
        outSample.mra = vec3(0.0, clamp(1.0 - p.glossinessFactor, 0.045, 1.0), 1.0);
    }
    else if (model == VULTRA_MAT_UNLIT)
    {
        MaterialParamsUnlit p = get_unlit_params(materialIndex);
        outSample.baseColor = p.color * sampleTextureGrad(p.colorTex, uv, duvdx, duvdy, vec4(1.0));
        outSample.unlit = true;
    }
    else if (model == VULTRA_MAT_PHONG)
    {
        MaterialParamsPhong p = get_phong_params(materialIndex);
        outSample.baseColor = p.diffuse * sampleTextureGrad(p.diffuseTex, uv, duvdx, duvdy, vec4(1.0));
        outSample.mra = vec3(0.0, clamp(1.0 / sqrt(max(p.specularShininess.w, 1.0)), 0.045, 1.0), 1.0);
    }

    outSample.mra.y = clamp(outSample.mra.y, 0.045, 1.0);
    return outSample;
}

float traceShadow(vec3 origin, vec3 direction, float tMax)
{
    shadowed = true;
    traceRayEXT(topLevelAS,
                gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT,
                0xff,
                0,
                0,
                1,
                origin,
                0.001,
                direction,
                tMax,
                1);
    return shadowed ? 0.0 : 1.0;
}

void main()
{
    const uint instanceIndex = gl_InstanceCustomIndexEXT;
    GPUInstanceData instance = instances[nonuniformEXT(instanceIndex)];

    const uint geomGlobalIndex = instance.geometryOffset + gl_GeometryIndexEXT;
    GPUGeometryNode node = geometryNodes[nonuniformEXT(geomGlobalIndex)];
    const uint materialIndex = instance.materialOffset + node.materialIndex;

    IndexBuffer ib = IndexBuffer(node.indexBufferAddress);
    const uint i0 = node.vertexOffset + ib.indices[gl_PrimitiveID * 3 + 0];
    const uint i1 = node.vertexOffset + ib.indices[gl_PrimitiveID * 3 + 1];
    const uint i2 = node.vertexOffset + ib.indices[gl_PrimitiveID * 3 + 2];

    const vec3 p0 = loadVec3(node.vertexBufferAddress, i0, node.vertexStrideBytes, node.positionOffsetBytes, vec3(0.0));
    const vec3 p1 = loadVec3(node.vertexBufferAddress, i1, node.vertexStrideBytes, node.positionOffsetBytes, vec3(0.0));
    const vec3 p2 = loadVec3(node.vertexBufferAddress, i2, node.vertexStrideBytes, node.positionOffsetBytes, vec3(0.0));
    const vec3 p0WS = (gl_ObjectToWorldEXT * vec4(p0, 1.0)).xyz;
    const vec3 p1WS = (gl_ObjectToWorldEXT * vec4(p1, 1.0)).xyz;
    const vec3 p2WS = (gl_ObjectToWorldEXT * vec4(p2, 1.0)).xyz;

    const vec3 geometricNormal = normalize(cross(p1WS - p0WS, p2WS - p0WS));
    const vec3 n0 = normalize(mat3(gl_ObjectToWorldEXT) * loadVec3(node.vertexBufferAddress, i0, node.vertexStrideBytes, node.normalOffsetBytes, geometricNormal));
    const vec3 n1 = normalize(mat3(gl_ObjectToWorldEXT) * loadVec3(node.vertexBufferAddress, i1, node.vertexStrideBytes, node.normalOffsetBytes, geometricNormal));
    const vec3 n2 = normalize(mat3(gl_ObjectToWorldEXT) * loadVec3(node.vertexBufferAddress, i2, node.vertexStrideBytes, node.normalOffsetBytes, geometricNormal));
    const vec2 uv0 = loadVec2(node.vertexBufferAddress, i0, node.vertexStrideBytes, node.texCoord0OffsetBytes, vec2(0.0));
    const vec2 uv1 = loadVec2(node.vertexBufferAddress, i1, node.vertexStrideBytes, node.texCoord0OffsetBytes, vec2(0.0));
    const vec2 uv2 = loadVec2(node.vertexBufferAddress, i2, node.vertexStrideBytes, node.texCoord0OffsetBytes, vec2(0.0));
    const vec4 t0 = loadVec4(node.vertexBufferAddress, i0, node.vertexStrideBytes, node.tangentOffsetBytes, vec4(1.0, 0.0, 0.0, 0.0));
    const vec4 t1 = loadVec4(node.vertexBufferAddress, i1, node.vertexStrideBytes, node.tangentOffsetBytes, vec4(1.0, 0.0, 0.0, 0.0));
    const vec4 t2 = loadVec4(node.vertexBufferAddress, i2, node.vertexStrideBytes, node.tangentOffsetBytes, vec4(1.0, 0.0, 0.0, 0.0));

    const float b0 = 1.0 - attribs.x - attribs.y;
    const vec3 normalWS = normalize(b0 * n0 + attribs.x * n1 + attribs.y * n2);
    const vec4 tangentWS = vec4(normalize(mat3(gl_ObjectToWorldEXT) * (b0 * t0.xyz + attribs.x * t1.xyz + attribs.y * t2.xyz)),
                                sign(b0 * t0.w + attribs.x * t1.w + attribs.y * t2.w));
    UvGrad uvGrad = calcUvGrad(p0WS, p1WS, p2WS, uv0, uv1, uv2);
    RtMaterialSample material = sampleMaterial(materialIndex, uvGrad.uv, uvGrad.dx, uvGrad.dy, normalWS, tangentWS);

    vec3 hitPos = b0 * p0WS + attribs.x * p1WS + attribs.y * p2WS;
    vec3 shaded = material.baseColor.rgb * 0.05;
    if (material.unlit)
    {
        hitValue.color = material.baseColor.rgb;
        return;
    }

    if (u_RtLights.counts.x > 0u)
    {
        vec3 lightDir = normalize(-u_RtLights.directional.directionShadowStrength.xyz);
        float ndotl = max(dot(material.normalWS, lightDir), 0.0);
        if (ndotl > 0.0)
        {
            float visibility = traceShadow(hitPos + material.normalWS * 0.002, lightDir, u_CameraBlock.data.zFar);
            visibility = mix(1.0, visibility, clamp(u_RtLights.directional.directionShadowStrength.w, 0.0, 1.0));
            shaded += material.baseColor.rgb * u_RtLights.directional.colorIntensity.rgb *
                      u_RtLights.directional.colorIntensity.w * ndotl * visibility;
        }
    }

    uint pointCount = min(u_RtLights.counts.y, 32u);
    for (uint i = 0u; i < pointCount; ++i)
    {
        RtPointLight light = u_RtLights.pointLights[i];
        vec3 toLight = light.posIntensity.xyz - hitPos;
        float dist = length(toLight);
        float radius = max(light.colorRadius.w, 0.001);
        if (dist <= 0.001 || dist > radius)
            continue;

        vec3 lightDir = toLight / dist;
        float ndotl = max(dot(material.normalWS, lightDir), 0.0);
        if (ndotl <= 0.0)
            continue;

        float attenuation = 1.0 - clamp(dist / radius, 0.0, 1.0);
        attenuation *= attenuation;
        float visibility = traceShadow(hitPos + material.normalWS * 0.002, lightDir, max(dist - 0.01, 0.001));
        shaded += material.baseColor.rgb * light.colorRadius.rgb * light.posIntensity.w * ndotl * attenuation * visibility;
    }

    hitValue.color = shaded;
}
