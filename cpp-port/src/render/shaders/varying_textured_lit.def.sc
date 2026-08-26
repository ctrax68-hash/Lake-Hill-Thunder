vec3 v_normal    : NORMAL    = vec3(0.0, 1.0, 0.0);
vec2 v_texcoord0 : TEXCOORD0 = vec2(0.0, 0.0);
// G25: world position for distance fog (see varying_lit.def.sc).
vec3 v_worldPos  : TEXCOORD1 = vec3(0.0, 0.0, 0.0);

vec3 a_position  : POSITION;
vec3 a_normal    : NORMAL;
vec2 a_texcoord0 : TEXCOORD0;
