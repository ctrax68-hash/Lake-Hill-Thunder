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
	vec3 color = texture2D(s_texColor, v_texcoord0).rgb;
	vec3 bright = max(color - vec3_splat(u_bloomParams.x), vec3_splat(0.0) );
	gl_FragColor = vec4(bright, 1.0);
}
