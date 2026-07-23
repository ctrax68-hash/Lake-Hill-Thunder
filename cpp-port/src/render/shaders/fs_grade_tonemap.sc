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
uniform vec4 u_gradeParams2; // x=saturation, y=vignetteInner, z=vignetteOuter

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
	vec3 scene = texture2D(s_texColor, v_texcoord0).rgb;
	vec3 bloom = texture2D(s_texBloom, v_texcoord0).rgb;
	vec3 color = scene + bloom * u_gradeParams1.x;

	color = pow(max(vec3_splat(0.0), color * u_gradeParams1.y + vec3_splat(u_gradeParams1.z) ), vec3_splat(1.0 / u_gradeParams1.w) );
	float luma = dot(color, vec3(0.299, 0.587, 0.114) );
	color = mix(vec3_splat(luma), color, u_gradeParams2.x);
	float d = length(v_texcoord0 - vec2(0.5, 0.5) ) * 1.4142135;
	color *= 1.0 - smoothstep(u_gradeParams2.y, u_gradeParams2.z, d);

	color = acesFilm(color);
	gl_FragColor = vec4(color, 1.0);
}
