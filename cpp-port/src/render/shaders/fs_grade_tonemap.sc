$input v_texcoord0

// Phase 5h (PORT_PROGRESS.md): the final postprocess pass -- additive
// bloom combine, then JS's own GradeShader math verbatim (index.html:
// 1533-1551: lift/gain/gamma, saturation, vignette), then an ACES filmic
// tonemap curve applied LAST (matching JS's RenderPass->Bloom->Grade->
// OutputPass ordering -- OutputPass's tonemap runs after the grade pass,
// not before). Combining bloom's additive composite into this same pass
// (rather than a separate fs_bloom_combine.sc) is a documented
// simplification: JS's UnrealBloomPass already composites internally
// before ShaderPass(GradeShader) even runs, so this port's equivalent
// combine step has no reason to be its own view/framebuffer round-trip.
//
// The ACES curve here is the standard Narkowicz (2015) filmic
// approximation, not the full ACRES reference transform -- the
// industry-standard cheap fit or this fixed-function look, and the same
// practical curve shape THREE.ACESFilmicToneMapping itself uses.
//
// No sRGB/linear color-space handling: unlike JS (`renderer.outputColorSpace
// = THREE.SRGBColorSpace`, plus OutputPass's own sRGB encode), this port's
// entire pipeline -- vertex colors, atlas/livery/sky textures, lighting
// math -- has never treated colors as linear-space values needing a
// gamma-encode step; adding one only here would double-apply gamma and
// wash the image out. Logged as a legitimate, intentional divergence from
// JS's real linear workflow, not an oversight.

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);
SAMPLER2D(s_texBloom, 1);
uniform vec4 u_gradeParams1; // x=bloomStrength, y=gain, z=lift, w=gamma
uniform vec4 u_gradeParams2; // x=saturation, y=vignetteInner, z=vignetteOuter, w=speedBlurAmt

vec3 acesFilm(vec3 x)
{
	float a = 2.51;
	float b = 0.03;
	float c = 2.43;
	float d = 0.59;
	float e = 0.14;
	return clamp((x*(a*x + b)) / (x*(c*x + d) + e), 0.0, 1.0);
}

void main()
{
	// s_texColor (sceneFb_) and s_texBloom (bloomBlurFb_) are both render
	// targets, same V-flip-on-read issue as fs_bloom_bright.sc's own sample
	// of sceneFb_ (see that shader's comment for the full explanation).
	// The vignette term below deliberately keeps using v_texcoord0
	// unflipped -- it's a screen-space effect (darken the actual rendered
	// frame's corners), not a texture sample, so it must follow this
	// fragment's real on-screen position, not the FBO-read correction.
	vec2 uv = vec2(v_texcoord0.x, 1.0 - v_texcoord0.y);

	// Phase H5 (PORT_PROGRESS.md): speed-driven radial blur. JS has no such
	// effect (its only speed-reactive visual is the chase-camera FOV push),
	// so this is a deliberate addition in the spirit of NT2003's feel, not a
	// literal port. Six taps walking from the pixel toward screen center,
	// unrolled per this codebase's shader convention (see fs_bloom_blur.sc)
	// rather than a GLSL loop. At u_gradeParams2.w == 0 every tap lands on
	// the same uv as the first, so this collapses to the exact single-sample
	// image with no branch needed -- verified absent at pit speed.
	vec2 toCenter = vec2(0.5, 0.5) - uv;
	float blurAmt = u_gradeParams2.w;
	vec3 scene = texture2D(s_texColor, uv).rgb;
	scene += texture2D(s_texColor, uv + toCenter * blurAmt * 0.2).rgb;
	scene += texture2D(s_texColor, uv + toCenter * blurAmt * 0.4).rgb;
	scene += texture2D(s_texColor, uv + toCenter * blurAmt * 0.6).rgb;
	scene += texture2D(s_texColor, uv + toCenter * blurAmt * 0.8).rgb;
	scene += texture2D(s_texColor, uv + toCenter * blurAmt * 1.0).rgb;
	scene *= (1.0 / 6.0);
	vec3 bloom = texture2D(s_texBloom, uv).rgb;
	vec3 color = scene + bloom * u_gradeParams1.x;

	color = pow(max(vec3_splat(0.0), color * u_gradeParams1.y + vec3_splat(u_gradeParams1.z) ), vec3_splat(1.0 / u_gradeParams1.w) );
	float luma = dot(color, vec3(0.299, 0.587, 0.114) );
	color = mix(vec3_splat(luma), color, u_gradeParams2.x);
	float d = length(v_texcoord0 - vec2(0.5, 0.5) ) * 1.4142135;
	color *= 1.0 - smoothstep(u_gradeParams2.y, u_gradeParams2.z, d);

	color = acesFilm(color);
	gl_FragColor = vec4(color, 1.0);
}
