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
    Node registers[32];
    uint blockIdx[4] = {0, 32, 64, 96};
    int count = 0;

    uint nodeIdx = imageLoad(headIndexImage, ivec2(gl_FragCoord.xy)).r;

    while (nodeIdx != 0xffffffff && count < MAX_FRAGMENT_COUNT)
    {
        fragments[count] = nodes[nodeIdx];
        nodeIdx = fragments[count].next;
        ++count;
    }

    uint groups = count / 32;
    // padding
    for (uint i = count; i < (groups + 1) * 32; ++i) {
        fragments[i] = Node(vec4(0, 0, 0, 0), 0.f, 0);
    }

    for (uint i = 0; i < groups + 1; ++i) {
        // load from local memory
        for (uint j = 0; j < 32; ++j) {
            registers[j] = fragments[i * 32 + j];
        }
        // sort
        for (uint j = 1; j < 32; ++j)
        {
            for (uint k = 31; k > 0; --k) {
                if (k <= j && registers[k].depth > registers[k - 1].depth) {
                    Node temp = registers[k];
                    registers[k] = registers[k - 1];
                    registers[k - 1] = temp;
                }
            }
        }
        // write back
        for (uint j = 0; j < 32; ++j) {
            fragments[i * 32 + j] = registers[j];
        }
    }

    // merge and blending
    vec4 color = vec4(0.025, 0.025, 0.025, 1.0f);
    for (int i = 0; i < count; ++i)
    {
        Node temp = Node(vec4(0, 0, 0, 0), 0.f, 0);
        uint b = 0;
        for (uint j = 0; j <= groups; ++j) {
            if (blockIdx[j] < (j + 1) * 32 && fragments[blockIdx[j]].depth > temp.depth) {
                b = j;
                temp = fragments[blockIdx[j]];
            }
        }
        blockIdx[b]++;
        color = mix(color, temp.color, temp.color.a);
    }

    outFragColor = color;
}