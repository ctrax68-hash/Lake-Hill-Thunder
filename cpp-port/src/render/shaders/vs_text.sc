$input a_position, a_color0, a_texcoord0
$output v_color0, v_texcoord0

// G26: UI text. Same pixel-space ortho transform every other UI primitive
// uses -- this differs from vs_flat only in carrying a UV through.

#include <bgfx_shader.sh>

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0) );
	v_color0 = a_color0;
	v_texcoord0 = a_texcoord0;
}
