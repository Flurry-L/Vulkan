#version 450

layout (early_fragment_tests) in;
layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inViewVec;
layout (location = 2) in vec3 inLightVec;
layout (location = 3) in vec4 inColor;

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
        float diffuse = max(dot(N, L), 0.0);
        float shininess = 32.f;
        float specular = pow(max(dot(R, V), 0.0), shininess);
        float ambient = 0.2f;
        float cos = max(dot(N, L), 0.0);
        //vec3 finalColor = (ambient + diffuse + specular) * pushConsts.color.xyz;
        //vec3 finalColor = (ambient + specular + diffuse) * inColor.xyz;
        vec3 finalColor = (ambient + cos) * inColor.xyz;
        nodes[nodeIdx].color = vec4(finalColor, 0.5);
        //nodes[nodeIdx].color = vec4(N, 1);
        nodes[nodeIdx].depth = gl_FragCoord.z;
        nodes[nodeIdx].next = prevHeadIdx;
    }
}