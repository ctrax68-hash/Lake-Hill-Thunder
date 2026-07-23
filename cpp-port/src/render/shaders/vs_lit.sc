$input a_position, a_normal, a_color0
$output v_color0, v_normal

// Phase 5a (PORT_PROGRESS.md): the first lit shader in this port -- a
// simple hemisphere-ambient + directional-diffuse model (see fs_lit.sc),
// replacing vs_flat.sc's unlit passthrough for world-space geometry (view
// 0). Normal is passed through untransformed rather than via a normal
// matrix: every draw call in this renderer uses an identity model
// transform (positions are already baked into world space per-vertex, see
// renderer.cpp), so there is no rotation/non-uniform-scale to correct for.

#include <bgfx_shader.sh>
#include <shaderlib.sh>

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0) );
	v_normal = a_normal;
	v_color0 = a_color0;
}
