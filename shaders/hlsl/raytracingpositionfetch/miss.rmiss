/* Copyright (c) 2024, Sascha Willems
 *
 * SPDX-License-Identifier: MIT
 *
 */

struct Payload
{
[[vk::location(0)]] float3 hitValue;
};

[shader("miss")]
void main(inout Payload p)
{
    p.hitValue = float3(0.0, 0.0, 0.2);
}