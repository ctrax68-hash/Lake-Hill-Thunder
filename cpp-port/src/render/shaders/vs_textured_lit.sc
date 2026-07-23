$input a_position, a_normal, a_texcoord0
$output v_normal, v_texcoord0

// Phase 5e (PORT_PROGRESS.md): textured-lit geometry (the crowd-tile-
// textured front stand tiers) -- same identity-model-transform rationale
// as vs_lit.sc for passing the normal through untransformed.

#include <bgfx_shader.sh>

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0) );
	v_normal = a_normal;
	v_texcoord0 = a_texcoord0;
}
