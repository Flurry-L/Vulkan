#pragma optionNV(inline all)
#extension GL_ARB_gpu_shader_int64 : require
#extension GL_NV_shader_atomic_int64 : require

void doBlendPacked(inout vec4 color, uint fragment)
{
  vec4 unpackedColor = unpackUnorm4x8(fragment);

  color = vec4((1 - unpackedColor.a) * color.rgb + unpackedColor.a * unpackedColor.rgb, 1);
}