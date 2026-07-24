$input a_position, a_normal, a_texcoord0, a_indices, a_weight
$output v_normal, v_texcoord0

#include <bgfx_shader.sh>

// Step 2 (PORT_PROGRESS.md, glTF skinned-mesh import pipeline): GPU linear-
// blend skinning -- each vertex's position/normal is transformed by a
// weighted sum of up to 4 bone matrices (u_boneMatrices, uploaded once per
// frame per SkinnedMesh instance via SkinnedMesh::setBoneMatrices()).
// MAX_BONES bounds the uniform array size passed to
// bgfx::createUniform(..., bgfx::UniformType::Mat4, MAX_BONES) --
// comfortably more joints than any wheel/suspension rig this project needs
// (Step 3: ~4 wheels + 4 suspension struts + a chassis root).
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

	// Normal transformed by the same skin matrix, no separate normal
	// matrix -- matches vs_textured_lit.sc's own simplicity (it passes
	// a_normal straight through untransformed); this project's rigs use
	// uniform scale, so the shortcut is equally valid here.
	v_normal = mul(skinMat, vec4(a_normal, 0.0) ).xyz;
	v_texcoord0 = a_texcoord0;
}
