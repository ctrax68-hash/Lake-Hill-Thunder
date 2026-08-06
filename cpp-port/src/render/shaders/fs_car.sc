$input v_normal, v_texcoord0, v_worldPos

// G2 (PORT_PROGRESS.md): car-specific shading. Keeps fs_textured_lit.sc's
// hemisphere-ambient + directional-diffuse term unchanged (so cars still
// match the lighting of everything else in the scene), and adds three
// terms that shader had none of, all of which NASCAR Thunder 2003's
// reviewers specifically called out about the car models (detailed paint,
// "shine and glimmer", chrome-like reflections):
//
//   - Blinn-Phong specular: a tight, dim highlight standing in for a
//     glossy clearcoat -- not aiming for a literal chrome/metal look,
//     just enough to kill the flat/matte read of pure N.L diffuse.
//   - Schlick-style Fresnel rim (pow(1-N.V, 5)): brightens grazing angles,
//     the classic "edge glow" cue of a curved glossy surface.
//   - A fake environment reflection: this renderer has no cubemap/IBL
//     infrastructure anywhere, so rather than inventing one, reuse the
//     two colors already used for the ambient hemisphere term
//     (u_hemiSky/u_hemiGround), but blended by the REFLECTION vector's Y
//     component instead of the surface normal's Y. That's the difference
//     between "restating ambient" and "a reflection sweep that moves
//     across the body as the camera orbits" -- driven by u_camPos (new
//     this phase; nothing before G2 needed the camera's world position on
//     the GPU since every other lit shader here is view-independent).
//     Blended in by the Fresnel term, so it's strongest at grazing angles
//     exactly where a real reflection would dominate.
//
// All constants below (specular power/strength, Fresnel power, reflection
// mix weight) are tuned empirically against close-up screenshots, not
// derived from a physical model -- this project has no measured reference
// BRDF to fit, and a cheap, plausible look is the actual goal.
//
// H6 (NT2003 engine-feel plan): a self-illumination term, added on top of
// everything above rather than a new material/pass. No emissive texture
// channel exists (or is needed) -- taillight red and the pace car's amber
// roof bar are both painted as specific, deliberately distinct RGB
// constants (livery.cpp's `taillight`/amber-bar fills), so this shader
// just recognizes those same constants by color-distance and treats a
// close match as "this texel is a lamp, light it regardless of the sun/
// hemisphere term". Cheap and texture-free, same "plausible look over a
// physical model" philosophy as this file's own fake-env-reflection term
// above. u_emissive.x is a constant taillight boost (every car); .y is the
// pace car's amber-bar pulse strength (0 for the field, animated in
// renderer.cpp) -- replacing G20's "painted bright enough to clear the
// bloom threshold" stand-in with a real, independently-animatable glow.
//
// H2 (NT2003 engine-feel plan) retune: these constants were fitted while
// gen_car_rig.py's body was a 4-corner box, i.e. every panel's normal was
// completely flat across its whole area. A spec power of 180 (an
// extremely tight hotspot) made sense there: it was the only way to keep
// the highlight from reading as a big flat lit rectangle covering an
// entire facet, since there was no normal variation within a facet to
// naturally localize it. H1 gave the body continuously-varying normals, so
// that job is now done by the geometry itself -- a tight power on top of
// that just makes the highlight vanishingly small and flickery as it
// sweeps across the now-curved surface. Lowered to 60 (a rounder,
// more-visible highlight, still nowhere near a diffuse-looking spread) and
// the reflection mix nudged up slightly (0.25->0.30) since the Fresnel
// term now varies smoothly across a real curve instead of jumping at
// facet boundaries, so a stronger reflection sweep no longer produces the
// hard per-facet banding the old geometry would have shown.

#include <bgfx_shader.sh>

uniform vec4 u_sunDir;
uniform vec4 u_sunColor;
uniform vec4 u_hemiSky;
uniform vec4 u_hemiGround;
uniform vec4 u_camPos;
uniform vec4 u_emissive; // x=taillight glow boost, y=pace amber-bar pulse

SAMPLER2D(s_texColor, 0);

void main()
{
	vec3 n = normalize(v_normal);
	vec3 viewDir = normalize(u_camPos.xyz - v_worldPos);
	vec3 lightDir = u_sunDir.xyz;
	vec3 halfDir = normalize(viewDir + lightDir);

	float hemiT = clamp(n.y * 0.5 + 0.5, 0.0, 1.0);
	vec3 ambient = mix(u_hemiGround.rgb, u_hemiSky.rgb, hemiT);
	float ndotl = max(dot(n, lightDir), 0.0);
	vec3 texel = texture2D(s_texColor, v_texcoord0).rgb;
	vec3 diffuse = texel * (ambient + u_sunColor.rgb * ndotl);

	float ndoth = max(dot(n, halfDir), 0.0);
	float spec = pow(ndoth, 60.0) * 0.35;

	float ndotv = max(dot(n, viewDir), 0.0);
	float fresnel = pow(1.0 - ndotv, 5.0);

	vec3 reflectDir = reflect(-viewDir, n);
	float reflT = clamp(reflectDir.y * 0.5 + 0.5, 0.0, 1.0);
	vec3 envColor = mix(u_hemiGround.rgb, u_hemiSky.rgb, reflT);

	// H6: color-match against livery.cpp's own taillight/amber-bar paint
	// constants (RGB, not affected by texture filtering enough to matter --
	// each is a solid-fill rect many texels wide). Squared distance kept
	// tight so no ordinary body/stripe/sponsor paint color can false-match.
	vec3 tailRef = vec3(150.0 / 255.0, 18.0 / 255.0, 15.0 / 255.0);
	vec3 amberRef = vec3(1.0, 0.70, 0.10);
	float tailD2 = dot(texel - tailRef, texel - tailRef);
	float amberD2 = dot(texel - amberRef, texel - amberRef);
	// smoothstep(edge0, edge1, x) is spec-undefined when edge0 >= edge1
	// (HLSL/GLSL both document this) -- bgfx cross-compiles this shader to
	// both, so the "1 at distance 0, 0 at distance >=0.01" falloff must be
	// built from the standard increasing-edge form, not by swapping the
	// smoothstep arguments themselves.
	float tailMatch = 1.0 - smoothstep(0.0, 0.01, tailD2);
	float amberMatch = 1.0 - smoothstep(0.0, 0.01, amberD2);
	vec3 emissive = texel * (tailMatch * u_emissive.x + amberMatch * u_emissive.y);

	vec3 rgb = mix(diffuse, envColor, fresnel * 0.30) + u_sunColor.rgb * spec + emissive;
	gl_FragColor = vec4(rgb, 1.0);
}
