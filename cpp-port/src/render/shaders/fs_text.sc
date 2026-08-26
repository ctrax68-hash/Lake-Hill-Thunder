$input v_color0, v_texcoord0

// G26: the glyph atlas stores COVERAGE, not colour -- tools/gen_font_atlas.py
// bakes an 8-bit greyscale mask, expanded to RGBA on decode. So the sampled
// value is used purely as an alpha multiplier and the visible colour comes
// entirely from the per-vertex tint. That is what lets one atlas serve white
// HUD text, a coloured car-number chip and a drop shadow without three copies.

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);

void main()
{
	float coverage = texture2D(s_texColor, v_texcoord0).r;
	gl_FragColor = vec4(v_color0.rgb, v_color0.a * coverage);
}
