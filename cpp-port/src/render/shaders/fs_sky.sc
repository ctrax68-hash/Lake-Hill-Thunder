$input v_texcoord0

// Phase 5c (PORT_PROGRESS.md): unlit texture sample -- the sky backdrop is
// a pre-painted "screen-space image", not a lit surface (matches JS, which
// sets it as `scene.background`, a plain texture never touched by any
// light).

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);

void main()
{
	gl_FragColor = texture2D(s_texColor, v_texcoord0);
}
