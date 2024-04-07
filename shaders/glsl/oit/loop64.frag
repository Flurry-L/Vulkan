#version 460
#extension GL_ARB_gpu_shader_int64 : require
#extension GL_NV_shader_atomic_int64 : require


#include "shaderCommon.glsl"
#define OIT_LAYERS 20

layout (early_fragment_tests) in;
layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inViewVec;
layout (location = 2) in vec3 inLightVec;

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
    vec4 color = vec4((ambient + diffuse + specular) * pushConsts.color.xyz, pushConsts.color.a);
    //const vec4 sRGBColor = unPremultLinearToSRGB(color);
    uint64_t zcur = packUint2x32(uvec2(packUnorm4x8(color), floatBitsToUint(gl_FragCoord.z)));
    bool evict = true;
    for (int i = 0; i < OIT_LAYERS; i++) {
        uint64_t ztest = atomicMin(kbuf[listPos + i * viewSize], zcur);
        if(ztest == packUint2x32(uvec2(0xFFFFFFFFu, 0xFFFFFFFFu)))
        {
            // We just inserted zcur into an empty space in the array
            evict = false;
            break;
        }
        zcur = (ztest > zcur) ? ztest : zcur;
    }
}