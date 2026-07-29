$input v_texcoord0

// Phase 5h (PORT_PROGRESS.md): bloom bright-pass -- a cheap linear-knee
// threshold (`max(color-threshold,0)`) standing in for UnrealBloomPass's
// real soft-knee luminance threshold. Sampled from the full-res scene
// color texture but rendered into a half-res target (renderer.cpp), so
// the GPU's own bilinear filtering does the downsample for free.
//
// Reuses vs_sky.sc/varying_sky.def.sc as its vertex stage (a fullscreen
// NDC quad with a UV passthrough) rather than adding a near-identical
// vs_fullscreen.sc -- sky's quad already is exactly that, so this is a
// zero-behavior-difference reuse, not a new abstraction.

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);
uniform vec4 u_bloomParams; // x = threshold

void main()
{
	// s_texColor here is sceneFb_'s color attachment (a render target),
	// not a CPU-uploaded 2D texture like the sky's own s_texColor sample in
	// fs_sky.sc -- sampling a texture that was itself just rasterized into
	// (rather than uploaded row-major from CPU memory) reads back
	// vertically flipped relative to how it was rendered (confirmed
	// empirically: bypassing this whole bloom/grade/tonemap chain and
	// blitting sceneFb_'s raw content straight to the backbuffer showed
	// the sky's zenith/horizon gradient inverted -- pale horizon-haze at
	// the top of the frame, rich zenith blue at the bottom -- while the
	// same content read directly, without a render-target round-trip,
	// was correctly oriented). Flip V back before sampling so this reads
	// the same way up as it was actually drawn.
	vec2 uv = vec2(v_texcoord0.x, 1.0 - v_texcoord0.y);
	vec3 color = texture2D(s_texColor, uv).rgb;
	vec3 bright = max(color - vec3_splat(u_bloomParams.x), vec3_splat(0.0) );
	gl_FragColor = vec4(bright, 1.0);
}
