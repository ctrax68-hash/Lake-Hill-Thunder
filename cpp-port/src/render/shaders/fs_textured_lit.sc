$input v_normal, v_texcoord0, v_worldPos

// Phase 5e (PORT_PROGRESS.md): identical hemisphere+directional lighting
// math to fs_lit.sc, but the base color comes from a sampled atlas texel
// instead of a per-vertex color -- used for the crowd-tile-textured front
// stand tiers. Phase 5h removed the temporary tonemap-stand-in clamp (see
// fs_lit.sc's own comment) now that the real offscreen ACES tonemap pass
// exists to do that roll-off instead.

#include <bgfx_shader.sh>
#include <shaderlib.sh>
#include "atmos.sh"

uniform vec4 u_sunDir;
uniform vec4 u_sunColor;
uniform vec4 u_hemiSky;
uniform vec4 u_hemiGround;
uniform vec4 u_camPos;

SAMPLER2D(s_texColor, 0);

void main()
{
	vec3 n = normalize(v_normal);
	float hemiT = clamp(n.y * 0.5 + 0.5, 0.0, 1.0);
	vec3 ambient = mix(u_hemiGround.rgb, u_hemiSky.rgb, hemiT);
	float ndotl = max(dot(n, u_sunDir.xyz), 0.0);
	vec3 lightAmt = lhtExpose(ambient + u_sunColor.rgb * ndotl);
	// G28: alpha comes from the texture now, instead of being hardcoded to
	// 1.0. This is what lets the catch fence be see-through between its
	// wires -- at its real 4.5-6.5 m height an opaque fence paints a grey
	// sheet over the grandstands, which is worse than having no fence.
	//
	// Safe for every other caller of this shader: the ribbon's asphalt
	// texture writes alpha 255 unconditionally (track_surface_texture.cpp),
	// and every region of the world atlas except the fence band is filled
	// opaque (atlas_texture.cpp). Only the fence is submitted with alpha
	// blending enabled at all; the rest draw with blending off, where the
	// output alpha is ignored regardless.
	vec4 texel = texture2D(s_texColor, v_texcoord0);
	vec3 lit = lhtHaze(texel.rgb * lightAmt, v_worldPos, u_camPos.xyz);
	gl_FragColor = vec4(lit, texel.a);
}
