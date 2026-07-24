vec3 v_normal    : NORMAL    = vec3(0.0, 1.0, 0.0);
vec2 v_texcoord0 : TEXCOORD0 = vec2(0.0, 0.0);

vec3 a_position : POSITION;
vec3 a_normal   : NORMAL;
vec2 a_texcoord0: TEXCOORD0;
// vec4, not uvec4: bgfx's vertex layout for this attribute (vertex_skinned.h)
// has no .asInt() marker, so it's bound via the normal float attribute path
// (GL_UNSIGNED_BYTE, non-normalized -- converts each byte's integer value to
// an equal float, e.g. 3 -> 3.0, same semantics cgltf_accessor_read_float()
// already uses for JOINTS_0). Declaring this uvec4 in the shader while bgfx
// binds it as floats is a type mismatch (found the hard way: every draw call
// silently rendered nothing under this project's headless GL backend, with
// every bgfx resource otherwise reporting valid).
vec4 a_indices  : BLENDINDICES;
vec4 a_weight   : BLENDWEIGHT;
