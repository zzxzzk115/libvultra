#ifndef MESH_CONSTANTS_GLSL
#define MESH_CONSTANTS_GLSL

struct Mesh {
    mat4 modelMatrix;
    uint materialIndex;
    uint enableNormalMapping;
    uint paddingU0;
    uint paddingU1;
};

layout (push_constant) uniform _MeshConstants { Mesh c_Mesh; };

mat4 getModelMatrix() { return c_Mesh.modelMatrix; }
uint getMaterialIndex() { return c_Mesh.materialIndex; }
uint getEnableNormalMapping() { return c_Mesh.enableNormalMapping; }

#endif