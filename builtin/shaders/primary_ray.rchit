#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require

#include "resources/light_block.glsl"
#include "resources/camera_block.glsl"
#include "lib/color.glsl"
#include "lib/bda_vertex.glsl"
#include "lib/pbr.glsl"
#include "lib/ltc.glsl"
// #include "lib/ray_cones.glsl"
#include "lib/ray_differentials.glsl"

layout(set = 3, binding = 0) uniform accelerationStructureEXT topLevelAS;

// LTC area light
layout(set = 3, binding = 2) uniform sampler2D t_LTCMat;
layout(set = 3, binding = 3) uniform sampler2D t_LTCMag;

// IBL
layout(set = 3, binding = 4) uniform sampler2D t_BrdfLUT;
layout(set = 3, binding = 5) uniform samplerCube t_IrradianceMap;
layout(set = 3, binding = 6) uniform samplerCube t_PrefilteredEnvMap;

struct GPUInstanceData {
    uint geometryOffset;
    uint geometryCount;
    uint materialOffset;
    uint materialCount;
};
layout(std430, set = 2, binding = 0) buffer InstanceData { GPUInstanceData instances[]; };

struct GPUMaterial {
    // --- texture indices ---
    uint albedoIndex;
    uint alphaMaskIndex;
    uint metallicIndex;
    uint roughnessIndex;

    uint specularIndex;
    uint normalIndex;
    uint aoIndex;
    uint emissiveIndex;

    uint metallicRoughnessIndex;
    uint paddingUI0; // ensure 16-byte alignment
    uint paddingUI1; // ensure 16-byte alignment
    uint paddingUI2; // ensure 16-byte alignment

    // --- color vectors ---
    vec4 baseColor;
    vec4 emissiveColorIntensity;
    vec4 ambientColor;

    // --- scalar material params ---
    float opacity;
    float metallicFactor;
    float roughnessFactor;
    float ior;

    float alphaCutoff;
    float paddingF0; // ensure 16-byte alignment
    float paddingF1; // ensure 16-byte alignment
    float paddingF2; // ensure 16-byte alignment

    // --- int params ---
    int alphaMode; // 0: OPAQUE, 1: MASK, 2: BLEND
    int doubleSided;
    int paddingI0; // ensure 16-byte alignment
    int paddingI1; // ensure 16-byte alignment
};
layout(std430, set = 2, binding = 1) buffer Materials { GPUMaterial materials[]; };

struct GPUGeometryNode {
    uint64_t vertexBufferAddress;
    uint64_t indexBufferAddress;
    uint materialIndex;
};
layout(std430, set = 2, binding = 2) buffer GeometryNodes { GPUGeometryNode geometryNodes[]; };

layout(set = 2, binding = 3) uniform sampler2D textures[];

layout(push_constant) uniform GlobalPushConstants
{
    vec4 missColor;
    float exposure;
    uint mode;
    uint enableNormalMapping;
    uint enableAreaLights;
    uint enableIBL;
    uint toneMappingMethod;
};

const uint MODE_ALBEDO = 0;
const uint MODE_NORMAL = 1;
const uint MODE_EMISSIVE = 2;
const uint MODE_MATALLIC = 3;
const uint MODE_ROUGHNESS = 4;
const uint MODE_AO = 5;
const uint MODE_DEPTH = 6;
const uint MODE_TEXTURE_LOD_DEBUG = 7;
const uint MODE_FINAL = 10;

struct HitValue {
    vec3 color;
    vec3 ddx;
    vec3 ddy;
};
layout(location = 0) rayPayloadInEXT HitValue hitValue;
layout(location = 1) rayPayloadEXT bool shadowed;
hitAttributeEXT vec2 attribs;

void main()
{
    const uint instanceIndex = gl_InstanceCustomIndexEXT;
    GPUInstanceData instance = instances[nonuniformEXT(instanceIndex)];

	const uint geomIndex = gl_GeometryIndexEXT;
    const uint geomGlobalIndex = instance.geometryOffset + geomIndex;

    GPUGeometryNode node = geometryNodes[nonuniformEXT(geomGlobalIndex)];
    const uint materialIndex = node.materialIndex;
    const uint materialGlobalIndex = instance.materialOffset + materialIndex;
    GPUMaterial mat = materials[nonuniformEXT(materialGlobalIndex)];

	Vertex v0 = fromBufferDeviceAddresses(node.vertexBufferAddress, node.indexBufferAddress, 0);
	Vertex v1 = fromBufferDeviceAddresses(node.vertexBufferAddress, node.indexBufferAddress, 1);
	Vertex v2 = fromBufferDeviceAddresses(node.vertexBufferAddress, node.indexBufferAddress, 2);

	vec2 uv = (1.0 - attribs.x - attribs.y) * v0.texCoord + attribs.x * v1.texCoord + attribs.y * v2.texCoord;

    vec3 N = normalize((1.0 - attribs.x - attribs.y) * v0.normal + attribs.x * v1.normal + attribs.y * v2.normal);
    vec3 T = normalize((1.0 - attribs.x - attribs.y) * v0.tangent.xyz + attribs.x * v1.tangent.xyz + attribs.y * v2.tangent.xyz);
    vec3 B = normalize(cross(N, T) * v0.tangent.w);
    mat3 TBN = mat3(T, B, N);
    vec3 viewDir = normalize(-gl_WorldRayDirectionEXT);
    vec3 fragPos = (1.0 - attribs.x - attribs.y) * v0.position + attribs.x * v1.position + attribs.y * v2.position;
    float dist = length(gl_WorldRayOriginEXT - fragPos);
    float depth = dist / u_Camera.far;

    // // Caculate LOD by using ray cones
    // Ray ray = makeRay(gl_WorldRayOriginEXT, gl_WorldRayDirectionEXT);
    // SurfaceHit surf = makeSurfaceHit(fragPos, N, dist, v0, v1, v2);
    // RayCone cone = initPrimaryRayCone(u_Camera.fovY, u_Camera.resolution.y);

    // ivec2 texSize = textureSize(textures[nonuniformEXT(mat.albedoIndex)], 0);
    // int texWidth = texSize.x;
    // int texHeight = texSize.y;

    // float lod = computeTextureLod(ray, surf, cone, v0, v1, v2, texWidth, texHeight);

    // Caculate LOD by using ray differentials
    UVDiff diff = computeUVDiffPrimary(gl_WorldRayOriginEXT, gl_WorldRayDirectionEXT, hitValue.ddx, hitValue.ddy, v0, v1, v2, attribs, textures[nonuniformEXT(mat.albedoIndex)]);
    float lod = diff.lod;

    vec3 emissiveColor = mat.emissiveColorIntensity.rgb;

    if (length(emissiveColor) > 0.0) {
        vec3 toneMappedColor = emissiveColor * exposure;
        if (toneMappingMethod == 0) toneMappedColor = toneMappingKhronosPbrNeutral(toneMappedColor);
        else if (toneMappingMethod == 1) toneMappedColor = linearTosRGB(toneMappingACES(toneMappedColor));
        else if (toneMappingMethod == 2) toneMappedColor = linearTosRGB(toneMappingReinhard(toneMappedColor));

        vec3 finalColor = linearTosRGB(toneMappedColor);

        if (mode == MODE_ALBEDO)      hitValue.color = vec3(0.0);
        else if (mode == MODE_NORMAL) hitValue.color = vec3(0.0);
        else if (mode == MODE_EMISSIVE) hitValue.color = toneMappedColor;
        else if (mode == MODE_MATALLIC) hitValue.color = vec3(0.0);
        else if (mode == MODE_ROUGHNESS) hitValue.color = vec3(1.0);
        else if (mode == MODE_AO)       hitValue.color = vec3(0.0);
        else if (mode == MODE_DEPTH)    hitValue.color = vec3(depth);
        else if (mode == MODE_TEXTURE_LOD_DEBUG) hitValue.color = vec3(0.0);
        else if (mode == MODE_FINAL) hitValue.color = finalColor;

        return;
    }

    vec3 albedo = mat.albedoIndex > 0 ? sRGBToLinear(textureLod(textures[nonuniformEXT(mat.albedoIndex)], uv, lod).rgb) : sRGBToLinear(mat.baseColor.rgb);
    vec3 emissive = mat.emissiveIndex > 0 ? sRGBToLinear(textureLod(textures[nonuniformEXT(mat.emissiveIndex)], uv, lod).rgb) : mat.emissiveColorIntensity.rgb * mat.emissiveColorIntensity.a;
    vec3 normal = textureLod(textures[nonuniformEXT(mat.normalIndex)], uv, lod).rgb;
    normal = normalize(normal * 2.0 - 1.0); // normal map
    normal = mat.normalIndex > 0 && enableNormalMapping == 1 ? normalize(TBN * normal) : N;
    float ao = textureLod(textures[nonuniformEXT(mat.aoIndex)], uv, lod).r;
    float metallic = textureLod(textures[nonuniformEXT(mat.metallicIndex)], uv, lod).r;
    float roughness = textureLod(textures[nonuniformEXT(mat.roughnessIndex)], uv, lod).r;
    if (mat.metallicRoughnessIndex > 0) {
        vec4 mrSample = textureLod(textures[nonuniformEXT(mat.metallicRoughnessIndex)], uv, lod);
        metallic = mrSample.b;
        roughness = mrSample.g;
    }

    metallic *= mat.metallicFactor;
    roughness *= mat.roughnessFactor;

    // Pack material properties
    PBRMaterial material;
	material.albedo = albedo;
    material.ao = ao;
    material.opacity = mat.opacity;
    material.emissive = emissive;
    material.metallic = metallic;
    material.roughness = roughness;

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, material.albedo, material.metallic);
    const vec3 diffuseColor = material.albedo * (1.0 - material.metallic);

    DirectionalLight light;
    light.direction = getLightDirection();
    light.color = getLightColor();
    light.intensity = getLightIntensity();

    vec3 Lo_dir = vec3(0.0);
    vec3 Lo_point = vec3(0.0);
    vec3 Lo_area = vec3(0.0);

    // Accumulate directional light contribution
    Lo_dir += calDirectionalLight(light, F0, normal, viewDir, material);

    // Accumulate point lights contribution
    int pointCount = getPointLightCount();
    for (int i = 0; i < pointCount; ++i) {
        PointLight pl = getPointLight(i);
        Lo_point += calPointLight(pl, F0, normal, viewDir, material, fragPos);
    }

    // Accumulate area lights contribution
    if (enableAreaLights == 1) {
        int areaCount = getAreaLightCount();
        for (int i = 0; i < areaCount; ++i) {
            AreaLight al = getAreaLight(i);
            vec3 center    = al.posIntensity.xyz;
            float intensity = al.posIntensity.w;
            vec3 U         = al.uTwoSided.xyz; // half-extent vector
            vec3 V         = al.vPadding.xyz;  // half-extent vector
            vec3 color     = al.color.rgb;
            bool twoSided  = (al.uTwoSided.w > 0.5);

            vec3 points[4];
            points[0] = center - U - V;
            points[1] = center + U - V;
            points[2] = center + U + V;
            points[3] = center - U + V;

            // Evaluate LTC
            LTCResult ltc = LTC_EvalRect(
                normal,        // N
                viewDir,       // V
                fragPos,       // P
                points,        // quad corners
                material.roughness,
                material.albedo,
                F0,            // specular F0
                twoSided,
                false,         // clipless off (use horizon clipping)
                t_LTCMat,
                t_LTCMag
            );

            // Accumulate area light contribution
            Lo_area += intensity * color * (ltc.spec + ltc.diff);
        }
    }

    // Calculate IBL contribution (ambient)
    vec3 Lo_ambient = vec3(0.0);
    if (enableIBL == 1) {
        Lo_ambient += calIBLAmbient(diffuseColor, F0, normal, viewDir, material, t_BrdfLUT, t_IrradianceMap, t_PrefilteredEnvMap);
    }

    // shadow test
    float tmin = 1e-5;
    float tmax = 10000.0;
	vec3 origin = fragPos + normal * 1e-5;
    shadowed = true;
    traceRayEXT(topLevelAS,
                gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT,
                0xff,
                0,
                0,
                1, // missGroupIndex = 1
                origin,
                tmin,
                -light.direction,
                tmax,
                1); // shadow payload location = 1

    if (shadowed)
    {
        Lo_dir = vec3(0.0);
    }
    vec3 Lo = Lo_dir + Lo_point + Lo_area + Lo_ambient;

    vec3 finalColor = Lo * exposure;
    if (toneMappingMethod == 0) finalColor = linearTosRGB(toneMappingKhronosPbrNeutral(finalColor));
    else if (toneMappingMethod == 1) finalColor = linearTosRGB(toneMappingACES(finalColor));
    else if (toneMappingMethod == 2) finalColor = linearTosRGB(toneMappingReinhard(finalColor));

    if (mode == MODE_ALBEDO)      hitValue.color = albedo;
    else if (mode == MODE_NORMAL) hitValue.color = normal;
    else if (mode == MODE_EMISSIVE) hitValue.color = emissive;
    else if (mode == MODE_MATALLIC) hitValue.color = vec3(metallic);
    else if (mode == MODE_ROUGHNESS) hitValue.color = vec3(roughness);
    else if (mode == MODE_AO)       hitValue.color = vec3(ao);
    else if (mode == MODE_DEPTH)    hitValue.color = vec3(depth);
    else if (mode == MODE_TEXTURE_LOD_DEBUG) hitValue.color = lodColors[int(lod)];
    else if (mode == MODE_FINAL) hitValue.color = finalColor;
}