$input v_normal, v_texcoord0

// Phase 5e (PORT_PROGRESS.md): identical hemisphere+directional lighting
// math to fs_lit.sc, but the base color comes from a sampled atlas texel
// instead of a per-vertex color -- used for the crowd-tile-textured front
// stand tiers. Phase 5h removed the temporary tonemap-stand-in clamp (see
// fs_lit.sc's own comment) now that the real offscreen ACES tonemap pass
// exists to do that roll-off instead.

#include <bgfx_shader.sh>
#include <shaderlib.sh>

uniform vec4 u_sunDir;
uniform vec4 u_sunColor;
uniform vec4 u_hemiSky;
uniform vec4 u_hemiGround;

SAMPLER2D(s_texColor, 0);

void main()
{
	vec3 n = normalize(v_normal);
	float hemiT = clamp(n.y * 0.5 + 0.5, 0.0, 1.0);
	vec3 ambient = mix(u_hemiGround.rgb, u_hemiSky.rgb, hemiT);
	float ndotl = max(dot(n, u_sunDir.xyz), 0.0);
	vec3 lightAmt = ambient + u_sunColor.rgb * ndotl;
	vec3 texel = texture2D(s_texColor, v_texcoord0).rgb;
	gl_FragColor = vec4(texel * lightAmt, 1.0);
}
