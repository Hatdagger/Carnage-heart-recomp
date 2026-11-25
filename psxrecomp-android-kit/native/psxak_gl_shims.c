/* Desktop-GL functions the runtime's GL renderer calls directly that GLES 3
 * does not have. The Android build uses the software renderer (presented by
 * SDL_Renderer on GLES), so these only keep libmain.so linkable; a GLES port
 * of the GL renderer would replace them. */
#include <GLES3/gl3.h>
#include <stdio.h>

void glClearDepth(double depth)
{
    glClearDepthf((GLfloat)depth);
}

/* Only used to read a texture back for the stereo (VR) path. */
void glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, void *pixels)
{
    (void)target; (void)level; (void)format; (void)type; (void)pixels;
    static int warned;
    if (!warned) {
        warned = 1;
        fprintf(stderr, "android: glGetTexImage is not available on GLES (ignored)\n");
    }
}
