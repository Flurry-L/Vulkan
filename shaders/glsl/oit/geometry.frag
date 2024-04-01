#version 450

layout (early_fragment_tests) in;
layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inViewVec;
layout (location = 2) in vec3 inLightVec;

struct Node
{
    vec4 color;
    float depth;
    uint next;
};

layout (set = 0, binding = 1) buffer GeometrySBO
{
    uint count;
    uint maxNodeCount;
};

layout (set = 0, binding = 2, r32ui) uniform coherent uimage2D headIndexImage;

layout (set = 0, binding = 3) buffer LinkedListSBO
{
    Node nodes[];
};

layout(push_constant) uniform PushConsts {
	mat4 model;
    vec4 color;
} pushConsts;

void main()
{
    // Increase the node count
    uint nodeIdx = atomicAdd(count, 1);

    // Check LinkedListSBO is full
    if (nodeIdx < maxNodeCount)
    {
        // Exchange new head index and previous head index
        uint prevHeadIdx = imageAtomicExchange(headIndexImage, ivec2(gl_FragCoord.xy), nodeIdx);

        // coumpute and store node data
        vec3 N = normalize(inNormal);
        vec3 L = normalize(inLightVec);
        vec3 V = normalize(inViewVec);
        vec3 R = reflect(-L, N);
        // 漫反射部分
        float diffuse = max(dot(N, L), 0.0);
        // 镜面反射部分
        float shininess = 100.f; // 高光的聚焦程度，较高的值会产生更小更亮的高光
        vec3 specular = pow(max(dot(R, V), 0.0), shininess) * vec3(1.0); // 白色高光
        // 环境光照部分
        vec3 ambient = vec3(0.1); // 较低的环境光照，模拟间接光照
        // 合并漫反射、镜面反射和环境光照
        vec3 lightColor = vec3(1.f);
        vec3 finalColor = (diffuse * pushConsts.color.xyz + specular) * lightColor + ambient;

        nodes[nodeIdx].color = vec4(finalColor, 1.0);
        nodes[nodeIdx].color = vec4(diffuse * pushConsts.color.xyz + specular, pushConsts.color.a);
        nodes[nodeIdx].depth = gl_FragCoord.z;
        nodes[nodeIdx].next = prevHeadIdx;
    }
}