/* OpenGL ES 3.2 support for the runtime's OpenGL renderer (Android).
 *
 * psxrecomp's gpu_gl_renderer.c targets desktop GL 3.3 core. Instead of
 * patching the submodule, the Android link wraps a handful of functions
 * (-Wl,--wrap=..., cmake/psxak.cmake) and bridges the differences:
 *
 *  - SDL_GL_SetAttribute: the runtime asks for a 3.3 *core* context; on
 *    Android that becomes an OpenGL ES 3.2 context. Requests for other
 *    profiles (SDL's own GLES renderer for the software path) are untouched.
 *  - SDL_GL_GetProcAddress (the renderer loads all modern entry points with
 *    it): glShaderSource runs each "#version 330" shader through
 *    psxak_gles_translate(); glBindFragDataLocationIndexed is a no-op (the
 *    translator writes layout(index=) instead); glMapBuffer maps with
 *    glMapBufferRange; desktop-only timer-query calls report "missing" so the
 *    renderer skips its GPU timing.
 *  - glTexImage2D / glTexSubImage2D: GLES has no GL_BGRA uploads into RGBA
 *    textures; such uploads are swizzled to RGBA on the CPU.
 *  - glReadPixels: GLES only guarantees RGBA/UNSIGNED_BYTE (normalized) and
 *    RGBA_INTEGER/UNSIGNED_INT (integer targets), plus one driver-chosen
 *    format. Other requests (BGRA, RGB, the renderer's R16UI VRAM mirror as
 *    RED_INTEGER/UNSIGNED_SHORT) read a supported format and convert.
 *  - gl_renderer_init_context: if the GL renderer still cannot start, the
 *    software renderer takes over at native resolution (the CPU cannot keep
 *    up at the 4x the player setting asks for).
 */
#include <SDL3/SDL.h>
#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psxak_gles_translate.h"
#include "gpu_render.h"

#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif

/* ---- context attributes --------------------------------------------------- */

bool __real_SDL_GL_SetAttribute(SDL_GLAttr attr, int value);
static int s_core_requested;

bool __wrap_SDL_GL_SetAttribute(SDL_GLAttr attr, int value)
{
    if (attr == SDL_GL_CONTEXT_PROFILE_MASK) {
        s_core_requested = (value == SDL_GL_CONTEXT_PROFILE_CORE ||
                            value == SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
        if (s_core_requested) value = SDL_GL_CONTEXT_PROFILE_ES;
    } else if (s_core_requested && attr == SDL_GL_CONTEXT_MAJOR_VERSION) {
        value = 3;
    } else if (s_core_requested && attr == SDL_GL_CONTEXT_MINOR_VERSION) {
        value = 2;
    }
    return __real_SDL_GL_SetAttribute(attr, value);
}

/* ---- shaders -------------------------------------------------------------- */

/* Stage of each shader object, for glShaderSource (ids are small integers). */
#define PSXAK_MAX_SHADER_ID 8192
static unsigned char s_shader_frag[PSXAK_MAX_SHADER_ID];
static int s_translated, s_logged_first;

static GLuint GL_APIENTRY psxak_glCreateShader(GLenum type)
{
    GLuint id = glCreateShader(type);
    if (id < PSXAK_MAX_SHADER_ID) s_shader_frag[id] = (type == GL_FRAGMENT_SHADER);
    return id;
}

static void GL_APIENTRY psxak_glShaderSource(GLuint sh, GLsizei count, const GLchar *const *str,
                                          const GLint *len)
{
    /* The renderer always passes one NUL-terminated string. */
    if (count == 1 && str && str[0] && (!len || len[0] < 0)) {
        int stage = (sh < PSXAK_MAX_SHADER_ID && s_shader_frag[sh]) ? PSXAK_GLES_FRAGMENT : PSXAK_GLES_VERTEX;
        char *es = psxak_gles_translate(stage, str[0]);
        if (es) {
            if (!s_logged_first) {
                s_logged_first = 1;
                printf("android gles: translating desktop GLSL 330 shaders to GLSL ES 3.10\n");
            }
            s_translated++;
            const GLchar *p = es;
            glShaderSource(sh, 1, &p, NULL);
            free(es);
            return;
        }
    }
    glShaderSource(sh, count, str, len);
}

static void GL_APIENTRY psxak_glBindFragDataLocationIndexed(GLuint prog, GLuint color, GLuint index,
                                                         const GLchar *name)
{
    /* Locations come from the translated shader's layout(location, index). */
    (void)prog; (void)color; (void)index; (void)name;
}

/* ---- buffers -------------------------------------------------------------- */

#ifndef GL_READ_ONLY
#define GL_READ_ONLY 0x88B8
#define GL_WRITE_ONLY 0x88B9
#define GL_READ_WRITE 0x88BA
#endif

static void *GL_APIENTRY psxak_glMapBuffer(GLenum target, GLenum access)
{
    GLint size = 0;
    glGetBufferParameteriv(target, GL_BUFFER_SIZE, &size);
    if (size <= 0) return NULL;
    GLbitfield bits = access == GL_READ_ONLY ? GL_MAP_READ_BIT
                    : access == GL_WRITE_ONLY ? GL_MAP_WRITE_BIT
                    : (GL_MAP_READ_BIT | GL_MAP_WRITE_BIT);
    return glMapBufferRange(target, 0, size, bits);
}

/* ---- proc address --------------------------------------------------------- */

SDL_FunctionPointer __real_SDL_GL_GetProcAddress(const char *proc);

SDL_FunctionPointer __wrap_SDL_GL_GetProcAddress(const char *proc)
{
    if (!proc) return NULL;
    if (!strcmp(proc, "glCreateShader")) return (SDL_FunctionPointer)psxak_glCreateShader;
    if (!strcmp(proc, "glShaderSource")) return (SDL_FunctionPointer)psxak_glShaderSource;
    if (!strcmp(proc, "glBindFragDataLocationIndexed"))
        return (SDL_FunctionPointer)psxak_glBindFragDataLocationIndexed;
    if (!strcmp(proc, "glMapBuffer")) return (SDL_FunctionPointer)psxak_glMapBuffer;
    /* Desktop timer queries: GLES has them only as EXT_disjoint_timer_query
     * with different rules. NULL makes the renderer skip GPU timing. eglGetProcAddress
     * may return a non-NULL stub for names a driver does not have, so these
     * are answered here. */
    if (!strcmp(proc, "glQueryCounter") || !strcmp(proc, "glGetQueryObjectui64v") ||
        !strcmp(proc, "glGetQueryObjectiv"))
        return NULL;
    return __real_SDL_GL_GetProcAddress(proc);
}

/* ---- texture uploads ------------------------------------------------------ */

static _Thread_local uint8_t *s_conv;
static _Thread_local size_t s_conv_cap;

static uint8_t *conv_buf(size_t n)
{
    if (n > s_conv_cap) {
        uint8_t *p = (uint8_t *)realloc(s_conv, n);
        if (!p) return NULL;
        s_conv = p;
        s_conv_cap = n;
    }
    return s_conv;
}

static GLint get_int(GLenum e)
{
    GLint v = 0;
    glGetIntegerv(e, &v);
    return v;
}

/* Tight RGBA copy of a BGRA upload; NULL if it can't be done on the CPU. */
static const void *bgra_to_rgba(GLsizei w, GLsizei h, const void *pixels)
{
    if (!pixels || w <= 0 || h <= 0) return pixels;
    if (get_int(GL_PIXEL_UNPACK_BUFFER_BINDING) != 0) return NULL;
    GLint row = get_int(GL_UNPACK_ROW_LENGTH);
    GLint skip_px = get_int(GL_UNPACK_SKIP_PIXELS), skip_rows = get_int(GL_UNPACK_SKIP_ROWS);
    size_t stride = (size_t)(row > 0 ? row : w) * 4;
    uint8_t *dst = conv_buf((size_t)w * (size_t)h * 4);
    if (!dst) return NULL;
    const uint8_t *src = (const uint8_t *)pixels + (size_t)skip_rows * stride + (size_t)skip_px * 4;
    for (GLsizei y = 0; y < h; y++) {
        const uint8_t *s = src + (size_t)y * stride;
        uint8_t *d = dst + (size_t)y * (size_t)w * 4;
        for (GLsizei x = 0; x < w; x++, s += 4, d += 4) {
            d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = s[3];
        }
    }
    return dst;
}

typedef struct { GLint row, skip_px, skip_rows, align; } Unpack;

static void unpack_tight(Unpack *saved)
{
    saved->row = get_int(GL_UNPACK_ROW_LENGTH);
    saved->skip_px = get_int(GL_UNPACK_SKIP_PIXELS);
    saved->skip_rows = get_int(GL_UNPACK_SKIP_ROWS);
    saved->align = get_int(GL_UNPACK_ALIGNMENT);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

static void unpack_restore(const Unpack *s)
{
    glPixelStorei(GL_UNPACK_ROW_LENGTH, s->row);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, s->skip_px);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, s->skip_rows);
    glPixelStorei(GL_UNPACK_ALIGNMENT, s->align);
}

static int s_warned_bgra_pbo;

void __real_glTexImage2D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h,
                         GLint border, GLenum format, GLenum type, const void *pixels);
void __wrap_glTexImage2D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h,
                         GLint border, GLenum format, GLenum type, const void *pixels)
{
    if (format == GL_BGRA && type == GL_UNSIGNED_BYTE) {
        if (internal == GL_RGBA || internal == GL_RGBA8 || internal == GL_BGRA) internal = GL_RGBA8;
        const void *p = bgra_to_rgba(w, h, pixels);
        if (pixels && !p) {
            if (!s_warned_bgra_pbo++) printf("android gles: BGRA upload from a buffer object (colours swapped)\n");
            __real_glTexImage2D(target, level, internal, w, h, border, GL_RGBA, type, pixels);
            return;
        }
        if (p && p != pixels) {
            Unpack u;
            unpack_tight(&u);
            __real_glTexImage2D(target, level, internal, w, h, border, GL_RGBA, type, p);
            unpack_restore(&u);
        } else {
            __real_glTexImage2D(target, level, internal, w, h, border, GL_RGBA, type, p);
        }
        return;
    }
    /* ES wants a sized internal format to match: GL_RGBA + RGBA/UBYTE is fine,
     * GL_RGBA8 + RGBA/UBYTE is fine; everything the renderer uses is valid. */
    __real_glTexImage2D(target, level, internal, w, h, border, format, type, pixels);
}

void __real_glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,
                            GLenum format, GLenum type, const void *pixels);
void __wrap_glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,
                            GLenum format, GLenum type, const void *pixels)
{
    if (format == GL_BGRA && type == GL_UNSIGNED_BYTE) {
        const void *p = bgra_to_rgba(w, h, pixels);
        if (!p) {
            if (!s_warned_bgra_pbo++) printf("android gles: BGRA upload from a buffer object (colours swapped)\n");
            __real_glTexSubImage2D(target, level, x, y, w, h, GL_RGBA, type, pixels);
            return;
        }
        Unpack u;
        unpack_tight(&u);
        __real_glTexSubImage2D(target, level, x, y, w, h, GL_RGBA, type, p);
        unpack_restore(&u);
        return;
    }
    __real_glTexSubImage2D(target, level, x, y, w, h, format, type, pixels);
}

/* ---- readback ------------------------------------------------------------- */

void __real_glReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type,
                         void *pixels);

static size_t pack_stride(GLsizei w, size_t bpp)
{
    GLint row = get_int(GL_PACK_ROW_LENGTH);
    GLint align = get_int(GL_PACK_ALIGNMENT);
    size_t s = (size_t)(row > 0 ? row : w) * bpp;
    if (align > 1) s = (s + (size_t)align - 1) / (size_t)align * (size_t)align;
    return s;
}

/* Read w*h RGBA8 or RGBA32UI (bpp 4 / 16) tightly into the conversion buffer. */
static void *read_tight(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, size_t bpp)
{
    void *tmp = conv_buf((size_t)w * (size_t)h * bpp);
    if (!tmp) return NULL;
    GLint row = get_int(GL_PACK_ROW_LENGTH), align = get_int(GL_PACK_ALIGNMENT);
    GLint skp = get_int(GL_PACK_SKIP_PIXELS), skr = get_int(GL_PACK_SKIP_ROWS);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    __real_glReadPixels(x, y, w, h, format, type, tmp);
    glPixelStorei(GL_PACK_ROW_LENGTH, row);
    glPixelStorei(GL_PACK_ALIGNMENT, align);
    glPixelStorei(GL_PACK_SKIP_PIXELS, skp);
    glPixelStorei(GL_PACK_SKIP_ROWS, skr);
    return tmp;
}

static int s_rp_logged;

void __wrap_glReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type,
                         void *pixels)
{
    if (w <= 0 || h <= 0 || !pixels || get_int(GL_PIXEL_PACK_BUFFER_BINDING) != 0 ||
        (format == GL_RGBA && type == GL_UNSIGNED_BYTE) ||
        (format == GL_RGBA_INTEGER && type == GL_UNSIGNED_INT)) {
        __real_glReadPixels(x, y, w, h, format, type, pixels);
        return;
    }
    /* The driver's own extra format for this framebuffer, if it matches. */
    if ((GLenum)get_int(GL_IMPLEMENTATION_COLOR_READ_FORMAT) == format &&
        (GLenum)get_int(GL_IMPLEMENTATION_COLOR_READ_TYPE) == type) {
        __real_glReadPixels(x, y, w, h, format, type, pixels);
        return;
    }
    if (type == GL_UNSIGNED_BYTE && (format == GL_BGRA || format == GL_RGB)) {
        const uint8_t *t = (const uint8_t *)read_tight(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, 4);
        if (!t) return;
        size_t bpp = format == GL_RGB ? 3 : 4;
        size_t stride = pack_stride(w, bpp);
        uint8_t *out = (uint8_t *)pixels;
        for (GLsizei r = 0; r < h; r++) {
            const uint8_t *s = t + (size_t)r * (size_t)w * 4;
            uint8_t *d = out + (size_t)r * stride;
            if (format == GL_RGB) {
                for (GLsizei c = 0; c < w; c++, s += 4, d += 3) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; }
            } else {
                for (GLsizei c = 0; c < w; c++, s += 4, d += 4) {
                    d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = s[3];
                }
            }
        }
        return;
    }
    if (format == GL_RED_INTEGER && type == GL_UNSIGNED_SHORT) {
        const uint32_t *t = (const uint32_t *)read_tight(x, y, w, h, GL_RGBA_INTEGER, GL_UNSIGNED_INT, 16);
        if (!t) return;
        size_t stride = pack_stride(w, 2);
        uint8_t *out = (uint8_t *)pixels;
        for (GLsizei r = 0; r < h; r++) {
            const uint32_t *s = t + (size_t)r * (size_t)w * 4;
            uint16_t *d = (uint16_t *)(out + (size_t)r * stride);
            for (GLsizei c = 0; c < w; c++) d[c] = (uint16_t)s[(size_t)c * 4];
        }
        return;
    }
    if (!s_rp_logged++)
        printf("android gles: glReadPixels format 0x%x type 0x%x passed through\n", format, type);
    __real_glReadPixels(x, y, w, h, format, type, pixels);
}

/* ---- renderer start ------------------------------------------------------- */

int __real_gl_renderer_init_context(SDL_Window *win);

int __wrap_gl_renderer_init_context(SDL_Window *win)
{
    int ok = __real_gl_renderer_init_context(win);
    if (ok) {
        printf("android gles: OpenGL ES renderer running (%d shaders translated)\n", s_translated);
    } else {
        printf("android gles: OpenGL ES renderer could not start; software renderer at native "
               "resolution\n");
        /* main.cpp switches to the software backend next; do it first so the
         * scale lands on the software rasterizer. */
        gr_set_backend(GR_BACKEND_SOFTWARE);
        gr_set_scale(1);
    }
    return ok;
}
