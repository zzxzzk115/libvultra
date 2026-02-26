#ifndef FRAME_BLOCK_GLSL
#define FRAME_BLOCK_GLSL

struct Frame {
    float time;
    float deltaTime;
};

layout (set = 0, binding = 0, std140) uniform _FrameBlock { Frame u_Frame; };

float getTime() { return u_Frame.time; }
float getDeltaTime() { return u_Frame.deltaTime; }

#endif