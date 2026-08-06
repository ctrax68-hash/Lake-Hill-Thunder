$input a_position, a_texcoord0, a_color0
$output v_texcoord0, v_color0

// H4 (NT2003 engine-feel plan): particle billboards. Vertices are already
// final world-space quad corners (renderer.cpp computes each one from the
// camera's own right/up basis extracted out of the view matrix) -- so,
// like every other shader here, this is just an MVP transform plus a
// straight passthrough of uv/color.

#include <bgfx_shader.sh>

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0) );
	v_texcoord0 = a_texcoord0;
	v_color0 = a_color0;
}
