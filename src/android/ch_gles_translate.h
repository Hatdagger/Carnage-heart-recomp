/* Desktop GLSL 3.30 -> GLSL ES 3.10 source translation for the runtime's
 * OpenGL renderer on Android (see ch_gles_translate.c). Pure string code, no
 * GL calls, so tools/research/gles_shader_check can test it on a PC. */
#ifndef CH_GLES_TRANSLATE_H
#define CH_GLES_TRANSLATE_H

#ifdef __cplusplus
extern "C" {
#endif

enum { CH_GLES_VERTEX = 0, CH_GLES_FRAGMENT = 1 };

/* Returns a malloc'd ES source when src starts with "#version 330", else NULL
 * (leave the source alone: SDL's own GLES shaders, the touch menu's 300 es
 * shaders). stage: CH_GLES_VERTEX or CH_GLES_FRAGMENT. */
char *ch_gles_translate(int stage, const char *src);

#ifdef __cplusplus
}
#endif
#endif
