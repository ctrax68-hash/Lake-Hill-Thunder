$input v_texcoord0

// Phase 5h (PORT_PROGRESS.md): a fixed-radius 3x3 binomial blur (weights
// 1-2-1 / 2-4-2 / 1-2-1, normalized by 16) -- the "small fixed-radius blur"
// the plan calls for, standing in for UnrealBloomPass's real multi-mip
// Gaussian chain. Single pass, both directions at once (not separable
// two-pass) since the kernel is small enough that the extra bandwidth of
// a combined 3x3 tap isn't worth a second view/framebuffer round-trip.

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);
uniform vec4 u_bloomParams; // y = texel width, z = texel height, w = radius scale

void main()
{
	vec2 texel = u_bloomParams.yz * u_bloomParams.w;
	vec3 c00 = texture2D(s_texColor, v_texcoord0 + vec2(-texel.x, -texel.y) ).rgb;
	vec3 c10 = texture2D(s_texColor, v_texcoord0 + vec2( 0.0,     -texel.y) ).rgb;
	vec3 c20 = texture2D(s_texColor, v_texcoord0 + vec2( texel.x, -texel.y) ).rgb;
	vec3 c01 = texture2D(s_texColor, v_texcoord0 + vec2(-texel.x,  0.0) ).rgb;
	vec3 c11 = texture2D(s_texColor, v_texcoord0).rgb;
	vec3 c21 = texture2D(s_texColor, v_texcoord0 + vec2( texel.x,  0.0) ).rgb;
	vec3 c02 = texture2D(s_texColor, v_texcoord0 + vec2(-texel.x,  texel.y) ).rgb;
	vec3 c12 = texture2D(s_texColor, v_texcoord0 + vec2( 0.0,      texel.y) ).rgb;
	vec3 c22 = texture2D(s_texColor, v_texcoord0 + vec2( texel.x,  texel.y) ).rgb;
	vec3 sum = (c00 + c20 + c02 + c22) + 2.0*(c10 + c01 + c21 + c12) + 4.0*c11;
	gl_FragColor = vec4(sum / 16.0, 1.0);
}
