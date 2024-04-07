#version 460
#extension GL_ARB_gpu_shader_int64 : require
#extension GL_NV_shader_atomic_int64 : require

#include "shaderCommon.glsl"
#define OIT_LAYERS 20

layout (location = 0) out vec4 outFragColor;

layout (set = 0, binding = 1) buffer LinkedListSBO
{
    uvec2 kbuf[];
};

layout (set = 0, binding = 2) uniform RenderPassUBO
{
    mat4 projection;
    mat4 view;
    vec4 lightPos;
    ivec3 viewport;
} renderPassUBO;

void main()
{
    const int viewSize = renderPassUBO.viewport.z;
    ivec2 coord = ivec2(gl_FragCoord.xy);
    const int listPos  = coord.y * renderPassUBO.viewport.x + coord.x;
    vec4 color = vec4(0);
    for (int i = 0; i < OIT_LAYERS; i++) {
        uvec2 stored = kbuf[listPos + i * viewSize];
        if (stored.y != 0xFFFFFFFFu) {
            doBlendPacked(color, stored.x);
        } else {
            break;
        }
    }
    //uvec2 stored = kbuf[listPos + 0 * viewSize];
    //if (stored.y != 0xFFFFFFFFu)
    outFragColor = color;

}