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
vec3 lhtHaze(vec3 color, vec3 worldPos, vec3 camPos)
{
	float dist = length(worldPos - camPos);
	float fog = 1.0 - exp(-dist * u_atmos.x);
	return mix(color, u_fogColor.rgb, clamp(fog, 0.0, 1.0));
}

#endif // LHT_ATMOS_SH
