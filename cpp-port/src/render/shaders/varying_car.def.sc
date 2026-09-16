vec3 v_normal    : NORMAL    = vec3(0.0, 1.0, 0.0);
vec2 v_texcoord0 : TEXCOORD0 = vec2(0.0, 0.0);
vec3 v_worldPos  : TEXCOORD1 = vec3(0.0, 0.0, 0.0);

vec3 a_position : POSITION;
vec3 a_normal   : NORMAL;
vec2 a_texcoord0: TEXCOORD0;
// FLOAT, not uvec4/Uint8 -- and this was measured, not reasoned.
//
// The previous pairing was `AttribType::Uint8` + `uvec4 a_indices`, on the
// argument that bgfx's GL backend picks glVertexAttribIPointer from
// `!isFloat(type) && !normalized`, so the shader side had to be an integer
// type to match. That reasoning is sound and the result still did not work:
// the joint indices never arrived, every wheel vertex read a garbage index,
// and the four wheels were absent from the render entirely. Isolated by
// bisection -- lifting bones 1-4 moved nothing, a constant `u_boneMatrices[1]`
// moved the whole car, and dropping the weight blend changed nothing, which
// together put the fault on a_indices and nowhere else.
//
// Storing the indices as FLOATS sidesteps the integer-attribute path on every
// backend: `isFloat(type)` is true, so bgfx binds with glVertexAttribPointer,
// which is the same path the position/normal/uv attributes already use and
// are known to work on. `int(a_indices.x)` is unchanged and still correct.
//
// Cost is 12 bytes per vertex on one shared 6856-vertex mesh, about 82 KB.
vec4 a_indices : BLENDINDICES;
vec4 a_weight   : BLENDWEIGHT;
