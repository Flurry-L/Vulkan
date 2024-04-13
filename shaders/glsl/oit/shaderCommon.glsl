#pragma optionNV(inline all)
#extension GL_ARB_gpu_shader_int64 : require
#extension GL_NV_shader_atomic_int64 : require
// Sets color to the result of blending color over baseColor.
// Color and baseColor are both premultiplied colors.
void doBlend(inout vec4 color, vec4 baseColor)
{
  color.rgb += (1 - color.a) * baseColor.rgb;
  color.a += (1 - color.a) * baseColor.a;
}

// Sets color to the result of blending color over fragment.
// Color and fragment are both premultiplied colors; fragment
// is an rgba8 sRGB unpremultiplied color packed in a 32-bit uint.
void doBlendPacked(inout vec4 color, uint fragment)
{
  vec4 unpackedColor = unpackUnorm4x8(fragment);
  unpackedColor.rgb *= unpackedColor.a;
  doBlend(color, unpackedColor);
}