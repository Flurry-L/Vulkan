#version 450

#define MAX_FRAGMENT_COUNT 8

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

    for (int i = (count - 2); i >= 0; --i) {
        for (int j = 0; j <= i; ++j) {
            if (fragments[j].depth < fragments[j + 1].depth) {
                Node temp = fragments[j];
                fragments[j] = fragments[j + 1];
                fragments[j + 1] = temp;
            }
        }
    }

    // Do blending
    vec4 color = vec4(1);
    for (int i = 0; i < count; ++i)
    {
        color = mix(color, fragments[i].color, fragments[i].color.a);
    }

    outFragColor = color;
}