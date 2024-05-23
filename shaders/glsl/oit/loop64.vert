#version 450

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec4 inColor;

layout (set = 0, binding = 0) uniform RenderPassUBO
{
    mat4 projection;
    mat4 view;
    vec4 lightPos;
    ivec3 viewport;
} renderPassUBO;

layout(push_constant) uniform PushConsts {
	mat4 model;
    vec4 color;
} pushConsts;

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec3 outViewVec;
layout (location = 2) out vec3 outLightVec;
layout (location = 3) out vec4 outColor;

void main()
{
    mat4 PVM = renderPassUBO.projection * renderPassUBO.view * pushConsts.model;
    gl_Position = PVM * vec4(inPos, 1.0);
    vec4 pos = renderPassUBO.view * pushConsts.model * vec4(inPos, 1.0);
    outNormal = mat3(renderPassUBO.view) * mat3(pushConsts.model) * inNormal;
    vec3 lPos = mat3(pushConsts.model) * renderPassUBO.lightPos.xyz;
    outLightVec = lPos - pos.xyz;
    outViewVec = -pos.xyz;
    outColor = inColor;
}
