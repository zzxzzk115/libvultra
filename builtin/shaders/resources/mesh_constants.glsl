#ifndef MESH_CONSTANTS_GLSL
#define MESH_CONSTANTS_GLSL

struct Mesh {
    mat4  modelMatrix;
    vec4  baseColor;
    float opacity;
    float metallicFactor;
    float roughnessFactor;

    bool useAlbedoTexture;
    bool useMetallicTexture;
    bool useRoughnessTexture;
    bool useNormalTexture;
    bool useAOTexture;
    bool useEmissiveTexture;
    bool useMetallicRoughnessTexture;
    bool useSpecularTexture;
};

layout (push_constant) uniform _MeshConstants { Mesh c_Mesh; };

mat4 getModelMatrix() { return c_Mesh.modelMatrix; }

#endif