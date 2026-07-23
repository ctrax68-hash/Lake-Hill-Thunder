$input v_color0, v_normal

// Phase 5a (PORT_PROGRESS.md): hemisphere-ambient + directional-diffuse
// shading -- a direct port of JS's two-light model (one THREE.HemisphereLight
// + one THREE.DirectionalLight, index.html:3576-3581/3520-3530), no shadow
// maps (JS has none either -- cars use a fake blob-shadow decal instead, not
// ported here since Phase 5's checklist doesn't call for it).
//
// u_sunColor/u_hemiSky/u_hemiGround already have their respective JS
// intensity multiplier baked in on the C++ side (env_presets.h, Phase 5b) --
// one fewer uniform than a literal port, a deliberate simplification with
// no visible difference since intensity only ever scales color linearly.
//
// Phase 5a itself hardcodes these uniforms to the 'noon-grass' preset's
// values (see renderer.cpp); Phase 5b makes them real per-track data.
//
// JS's own light intensities (e.g. sun=3.2, hemi=1.1) are calibrated to be
// used WITH ACES filmic tonemapping (THREE.ACESFilmicToneMapping,
// index.html:1508), which gracefully compresses over-bright values back
// into a displayable range -- that tonemap pass is Phase 5h's job, not
// this sub-phase's. Writing those same intensities straight to an LDR
// backbuffer with no tonemap at all would clip almost everything to a
// uniform blown-out near-white, hiding the very shading variation (banked
// vs. flat surfaces) Phase 5a exists to make visible. Clamping the
// light sum to 1.0 here is a deliberate, temporary stand-in -- logged in
// PORT_PROGRESS.md -- until Phase 5h's real tonemap pass replaces it.

#include <bgfx_shader.sh>
#include <shaderlib.sh>

uniform vec4 u_sunDir;     // xyz: unit direction TOWARD the sun
uniform vec4 u_sunColor;   // rgb: sun color * intensity
uniform vec4 u_hemiSky;    // rgb: hemisphere sky color * intensity
uniform vec4 u_hemiGround; // rgb: hemisphere ground color * intensity

void main()
{
	vec3 n = normalize(v_normal);
	float hemiT = clamp(n.y * 0.5 + 0.5, 0.0, 1.0);
	vec3 ambient = mix(u_hemiGround.rgb, u_hemiSky.rgb, hemiT);
	float ndotl = max(dot(n, u_sunDir.xyz), 0.0);
	vec3 lightAmt = min(ambient + u_sunColor.rgb * ndotl, vec3(1.0, 1.0, 1.0));
	vec3 lit = v_color0.rgb * lightAmt;
	gl_FragColor = vec4(lit, v_color0.a);
}
