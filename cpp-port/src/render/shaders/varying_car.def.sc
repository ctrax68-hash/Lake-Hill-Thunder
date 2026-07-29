vec3 v_normal    : NORMAL    = vec3(0.0, 1.0, 0.0);
vec2 v_texcoord0 : TEXCOORD0 = vec2(0.0, 0.0);
vec3 v_worldPos  : TEXCOORD1 = vec3(0.0, 0.0, 0.0);

vec3 a_position : POSITION;
vec3 a_normal   : NORMAL;
vec2 a_texcoord0: TEXCOORD0;
// uvec4, not vec4 -- see varying_skinned.def.sc's own long comment on why;
// this is the same skinned car rig, same attribute layout.
uvec4 a_indices : BLENDINDICES;
vec4 a_weight   : BLENDWEIGHT;
