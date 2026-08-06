$input v_texcoord0, v_color0

// H4 (NT2003 engine-feel plan): samples particle_texture.h's radial-
// gradient sprite (pure white, alpha-only falloff) and tints/modulates by
// the per-vertex color -- v_color0.rgb is the particle's own tint,
// v_color0.a is the batch's fixed opacity (0.38 smoke / 0.9 spark, JS's
// own uAlpha uniforms), baked per-vertex instead of a real uniform so one
// shader serves both batches.
//
// Output is PREMULTIPLIED (rgb already scaled by the combined alpha), not
// straight alpha -- the two batches use different blend equations
// (renderer.cpp: BGFX_STATE_BLEND_NORMAL for smoke, BGFX_STATE_BLEND_ADD
// for sparks) and premultiplied output is the one formulation that's
// correct under both without a shader permutation: BGFX_STATE_BLEND_NORMAL
// is (ONE, INV_SRC_ALPHA) -- exactly the premultiplied-alpha blend
// equation -- and BGFX_STATE_BLEND_ADD (ONE, ONE) ignores the alpha
// channel as a blend factor entirely, so an additive particle's
// brightness has to already be baked into rgb or it would ignore the
// sprite's own radial falloff and the batch's opacity.

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);

void main()
{
	vec4 texel = texture2D(s_texColor, v_texcoord0);
	float a = texel.a * v_color0.a;
	gl_FragColor = vec4(texel.rgb * v_color0.rgb * a, a);
}
