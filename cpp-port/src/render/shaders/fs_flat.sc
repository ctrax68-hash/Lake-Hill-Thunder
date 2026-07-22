$input v_color0

// See vs_flat.sc: flat vertex-color passthrough, no lighting yet.

#include <bgfx_shader.sh>
#include <shaderlib.sh>

void main()
{
	gl_FragColor = v_color0;
}
