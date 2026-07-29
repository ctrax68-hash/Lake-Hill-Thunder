vec3 v_normal    : NORMAL    = vec3(0.0, 1.0, 0.0);
vec2 v_texcoord0 : TEXCOORD0 = vec2(0.0, 0.0);

vec3 a_position : POSITION;
vec3 a_normal   : NORMAL;
vec2 a_texcoord0: TEXCOORD0;
// uvec4, not vec4: renderer_gl.cpp's ProgramGL::bindAttributes() decides
// glVertexAttribIPointer vs. glVertexAttribPointer purely from the vertex
// layout's `type`/`normalized` fields (`!isFloat(type) && !normalized` ->
// IPointer) -- the `_asInt` flag `vertex_skinned.h`'s `.add()` call leaves at
// its default is never consulted there at all (it only affects the CPU-side
// vertexPack/vertexConvert helpers, unused here). vertex_skinned.h's Indices
// attribute is `AttribType::Uint8`, non-normalized, so under WebGL2/GLES3
// bgfx *always* binds it via IPointer regardless of that flag. Declaring
// this `vec4` (float) was a genuine type mismatch against that integer
// binding -- harmless on the desktop GL driver used for local screenshot
// verification (permissive enough to let the draw through anyway), but a
// real `GL_INVALID_OPERATION` under Chromium/WebKit's stricter WebGL2
// validation on every single per-car `glDrawElementsInstanced` call, which
// silently drops the draw -- explains a long-reported "no cars ever appear"
// class of complaint that earlier sessions attributed to the camera instead
// (this exact warning was even seen and dismissed as "pre-existing,
// unrelated" in an earlier PORT_PROGRESS.md entry). `uvec4` matches the
// actual GL_UNSIGNED_BYTE + IPointer binding; `vs_skinned.sc`'s existing
// `int(a_indices.x)` casts still work unchanged (int(uint) is a valid GLSL
// conversion).
uvec4 a_indices : BLENDINDICES;
vec4 a_weight   : BLENDWEIGHT;
