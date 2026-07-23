$input a_position, a_texcoord0
$output v_texcoord0

// Phase 5c (PORT_PROGRESS.md): the sky background's fullscreen quad.
// renderer.cpp submits this with identity view/proj (the quad's own
// vertex positions are already in clip space, -1..1), so
// u_modelViewProj reduces to identity here -- kept as a real matrix
// multiply anyway (not a passthrough) for consistency with every other
// shader in this port, all of which always go through u_modelViewProj.

#include <bgfx_shader.sh>

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0) );
	v_texcoord0 = a_texcoord0;
}
