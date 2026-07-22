$input a_position, a_color0
$output v_color0

// Phase 2 minimal renderer: flat-shaded, no lighting -- the whole scene
// (track ribbon + car boxes) is already in world XY, so this is just an
// MVP transform + vertex-color passthrough. See PORT_PROGRESS.md's Phase 2
// notes; full lighting/materials are Phase 5 territory.

#include <bgfx_shader.sh>
#include <shaderlib.sh>

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0) );
	v_color0 = a_color0;
}
