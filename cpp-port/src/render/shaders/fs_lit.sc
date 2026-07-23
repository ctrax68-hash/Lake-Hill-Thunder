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
// into a displayable range. Phase 5a-5g's `min(..., vec3(1.0))` hard clamp
// was a temporary stand-in for that missing tonemap pass (logged in
// PORT_PROGRESS.md at the time); Phase 5h adds the real offscreen-FBO +
// ACES tonemap chain (renderer.cpp/fs_grade_tonemap.sc), so this shader now
// writes its lit color unclamped, above 1.0 where the lighting math wants
// it to be, and lets that later pass do the graceful roll-off instead.

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
	vec3 lightAmt = ambient + u_sunColor.rgb * ndotl;
	vec3 lit = v_color0.rgb * lightAmt;
	gl_FragColor = vec4(lit, v_color0.a);
}
