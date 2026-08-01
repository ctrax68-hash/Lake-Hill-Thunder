$input a_position, a_normal, a_texcoord0, a_indices, a_weight
$output v_normal, v_texcoord0, v_worldPos

#include <bgfx_shader.sh>

// G2 (PORT_PROGRESS.md): identical GPU linear-blend skinning to
// vs_skinned.sc, plus one extra output (v_worldPos) so fs_car.sc can build
// a per-pixel view vector for its specular/Fresnel/fake-env-reflection
// terms -- vs_skinned.sc/fs_textured_lit.sc never needed the camera's
// position, so that pair is left untouched and still serves the crowd-atlas
// stand seats; cars get their own shader pair instead.
#define MAX_BONES 32
uniform mat4 u_boneMatrices[MAX_BONES];

void main()
{
	mat4 skinMat = u_boneMatrices[int(a_indices.x)] * a_weight.x
	             + u_boneMatrices[int(a_indices.y)] * a_weight.y
	             + u_boneMatrices[int(a_indices.z)] * a_weight.z
	             + u_boneMatrices[int(a_indices.w)] * a_weight.w;

	vec4 worldPos = mul(skinMat, vec4(a_position, 1.0) );
	gl_Position = mul(u_modelViewProj, worldPos);

	// worldPos/skinMat above are only skin (bone) space -- wheel spin/
	// steering relative to the chassis, not this car's actual position/
	// heading on the track. fs_car.sc needs a TRUE world-space position and
	// normal to build a camera-relative view vector (u_camPos is real world
	// space), so also apply the per-object model matrix bgfx exposes as the
	// built-in u_model[0] (the same matrix set via bgfx::setTransform()
	// right before this car's draw call).
	vec4 trueWorldPos = mul(u_model[0], worldPos);
	v_normal = mul(u_model[0], vec4(mul(skinMat, vec4(a_normal, 0.0) ).xyz, 0.0) ).xyz;
	v_texcoord0 = a_texcoord0;
	v_worldPos = trueWorldPos.xyz;
}
