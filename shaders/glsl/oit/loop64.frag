#version 460
#extension GL_ARB_gpu_shader_int64 : require
#extension GL_NV_shader_atomic_int64 : require
#extension GL_GOOGLE_include_directive : require

#include "shaderCommon.glsl"
//#define OIT_LAYERS 4

layout (early_fragment_tests) in;
layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inViewVec;
layout (location = 2) in vec3 inLightVec;
layout (location = 3) in vec4 inColor;

layout (set = 0, binding = 0) uniform RenderPassUBO
{
    mat4 projection;
    mat4 view;
    vec4 lightPos;
    ivec3 viewport;
} renderPassUBO;

layout (set = 0, binding = 3) buffer LinkedListSBO
{
    uint64_t kbuf[];
};

layout(push_constant) uniform PushConsts {
	mat4 model;
    vec4 color;
} pushConsts;
void main()
{
    const int viewSize = renderPassUBO.viewport.z;
    ivec2 coord = ivec2(gl_FragCoord.xy);
    const int listPos  = coord.y * renderPassUBO.viewport.x + coord.x;

    // coumpute and store node data
    vec3 N = normalize(inNormal);
    vec3 L = normalize(inLightVec);
    vec3 V = normalize(inViewVec);
    vec3 R = reflect(-L, N);
    float diffuse = max(dot(N, L), 0.0);
    float shininess = 32.f;
    float specular = pow(max(dot(R, V), 0.0), shininess);
    float ambient = 0.2f;
    float cos = max(dot(N, L), 0.0);

    vec4 color = vec4((ambient + cos) * inColor.xyz, 0.5);
    //vec4 color = vec4((ambient + cos) * vec3(255.f/255, 204.f/255, 153.f/255), 0.5);

    uint64_t ztest = packUint2x32(uvec2(packUnorm4x8(color), floatBitsToUint(gl_FragCoord.z)));
    for (int i = 0; i < OIT_LAYERS; i++) {
        //uint64_t zold = atomicMin(kbuf[listPos + i * viewSize], ztest);
        uint64_t zold = atomicMin(kbuf[listPos * OIT_LAYERS + i], ztest);
        if(zold == packUint2x32(uvec2(0xFFFFFFFFu, 0xFFFFFFFFu)) || zold == ztest)
        {
            break;
        }
        ztest = (zold > ztest) ? zold : ztest;
    }
}