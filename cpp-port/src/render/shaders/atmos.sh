#ifndef LHT_ATMOS_SH
#define LHT_ATMOS_SH

// G25 (NASCAR Thunder graphics pass): exposure + distance haze, shared by
// every lit surface so the three copies of the lighting math cannot drift.
//
// WHY EXPOSURE EXISTS. The lighting model is `texel * (ambient + sun*ndotl)`
// with intensities inherited from the JS original (env_presets.h). On a
// flat-up surface under the noon preset that multiplier lands around 3.4x, so
// 0.25-albedo asphalt arrives near 0.86 before tonemapping -- which is exactly
// why the track has always read bright and flat. L3 measured this, named it,
// and deliberately left it alone because it touches every lit surface in the
// game and needed its own before/after pass. This is that pass. Rather than
// rewriting four preset tables, u_atmos.y scales the light amount once, in one
// place, so the presets keep their relative moods.
//
// WHY HAZE EXISTS. The reference footage's depth comes almost entirely from
// atmospheric perspective -- distant stands, fences and treelines desaturate
// into the sky. This renderer had no fog of any kind, so everything sat at
// full contrast to the horizon and read flat and papery. Exponential falloff
// (not linear) because it has no visible "start" plane to give the effect away.

uniform vec4 u_atmos;    // x = fog density (1/m), y = exposure, zw unused
uniform vec4 u_fogColor; // rgb = haze colour (matched to the sky gradient)

// Applies exposure to the light amount BEFORE it multiplies albedo, so this
// behaves like a camera stop rather than a colour filter.
vec3 lhtExpose(vec3 lightAmt)
{
	return lightAmt * u_atmos.y;
}

// Blends a lit colour toward the haze by camera distance. Called last, after
// everything else including specular -- haze sits between the eye and the
// surface, so it must dim highlights too, not be added underneath them.
// G31: the blend is CAPPED. Pure exponential falloff does not desaturate the
// far field, it erases it -- at the shipped 0.0016/m density the blend is 0.91
// at 1500 m and 0.99 by 3000 m, i.e. distant geometry becomes literally the
// fog colour and stops existing.
//
// That has already cost this project twice. The treeline added for depth was
// invisible at its first placement (980/880 m, a 79% blend) and had to be
// moved to 620/520 m and darkened. And on Big Sable -- 2600 m round -- the
// far side of the circuit, and the whole scene from the top-down camera, wash
// out to flat grey-green.
//
// 0.82 is chosen so nothing under about 1050 m changes at all: the near and
// mid field, which is every G25 measurement and every screenshot the haze was
// tuned against, is bit-identical. Beyond that, distant stands and treelines
// stay faintly present instead of vanishing, which is what the reference
// footage actually shows -- desaturated to near-sky, but still there.
#define LHT_MAX_HAZE 0.82

vec3 lhtHaze(vec3 color, vec3 worldPos, vec3 camPos)
{
	float dist = length(worldPos - camPos);
	float fog = 1.0 - exp(-dist * u_atmos.x);
	return mix(color, u_fogColor.rgb, clamp(fog, 0.0, LHT_MAX_HAZE));
}

#endif // LHT_ATMOS_SH
