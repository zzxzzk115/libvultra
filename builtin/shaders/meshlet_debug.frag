#version 460 core

layout(location = 0) in flat uint meshletID;
layout(location = 0) out vec4 outColor;

vec3 hashColor(uint id)
{
    uint n = id * 1664525u + 1013904223u;
    return vec3((n & 0xFFu), (n >> 8) & 0xFFu, (n >> 16) & 0xFFu) / 255.0;
}

void main()
{
    outColor = vec4(hashColor(meshletID), 1.0);
}