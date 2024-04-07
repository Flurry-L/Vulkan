#version 450

#define MAX_FRAGMENT_COUNT 128

struct Node
{
    vec4 color;
    float depth;
    uint next;
};

layout (location = 0) out vec4 outFragColor;

layout (set = 0, binding = 0, r32ui) uniform uimage2D headIndexImage;

layout (set = 0, binding = 1) buffer LinkedListSBO
{
    Node nodes[];
};

void main()
{
    Node fragments[MAX_FRAGMENT_COUNT];
    int count = 0;

    uint nodeIdx = imageLoad(headIndexImage, ivec2(gl_FragCoord.xy)).r;

    while (nodeIdx != 0xffffffff && count < MAX_FRAGMENT_COUNT)
    {
        fragments[count] = nodes[nodeIdx];
        nodeIdx = fragments[count].next;
        ++count;
    }

    // Do the insertion sort
    for (uint j = 1; j < count; ++j)
    {
        for (uint i = MAX_FRAGMENT_COUNT - 1; i > 0; --i) {
            if (i <= j && fragments[i].depth > fragments[i - 1].depth) {
                Node temp = fragments[i];
                fragments[i] = fragments[i - 1];
                fragments[i - 1] = temp;
            }
        }
    }

    // Do blending
    vec4 color = vec4(0.7529f, 0.7529f, 0.7529f, 1.f);
    for (int i = 0; i < count; ++i)
    {
        color = mix(color, fragments[i].color, fragments[i].color.a);
    }

    outFragColor = color;
}