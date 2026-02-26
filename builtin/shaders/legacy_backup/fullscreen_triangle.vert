#version 460 core

// https://www.saschawillems.de/blog/2016/08/13/vulkan-tutorial-on-rendering-a-fullscreen-quad-without-buffers/

layout (location = 0) out vec2 v_TexCoord;

void main() {
    v_TexCoord = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(v_TexCoord * 2.0 - 1.0, 0.0, 1.0);
}