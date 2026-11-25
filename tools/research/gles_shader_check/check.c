/* Compile + link the runtime's GL renderer shaders as GLSL ES on a real
 * OpenGL ES 3.2 driver (Mesa, headless "surfaceless" EGL), after the Android
 * translation in psxrecomp-android-kit/native/psxak_gles_translate.c.
 *
 *   python3 tools/research/gles_shader_check/extract.py PREPROCESSED.c shaders.bin
 *   cc -O1 -o check check.c ../../../psxrecomp-android-kit/native/psxak_gles_translate.c -ldl
 *   ./check shaders.bin
 *
 * shaders.bin: records "name\0stage\0source\0" (stage "v" or "f"). EGL/GLES
 * are loaded with dlopen so no -dev packages are needed. Exit 0 = all OK.
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../../psxrecomp-android-kit/native/psxak_gles_translate.h"

typedef void *EGLDisplay, *EGLConfig, *EGLContext, *EGLSurface;
typedef int32_t EGLint;
typedef unsigned EGLBoolean, EGLenum, GLenum, GLuint;
typedef int GLint, GLsizei;
typedef char GLchar;

#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_OPENGL_ES3_BIT 0x0040
#define EGL_SURFACE_TYPE 0x3033
#define EGL_PBUFFER_BIT 0x0001
#define EGL_NONE 0x3038
#define EGL_OPENGL_ES_API 0x30A0
#define EGL_CONTEXT_MAJOR_VERSION 0x3098
#define EGL_CONTEXT_MINOR_VERSION 0x30FB
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_VERSION 0x1F02
#define GL_RENDERER 0x1F01
#define GL_EXTENSIONS 0x1F03

static void *(*eglGetProcAddress_)(const char *);
#define P(ret, name, args) static ret (*name) args
P(EGLDisplay, eglGetPlatformDisplayEXT_, (EGLenum, void *, const EGLint *));
P(EGLBoolean, eglInitialize_, (EGLDisplay, EGLint *, EGLint *));
P(EGLBoolean, eglChooseConfig_, (EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *));
P(EGLBoolean, eglBindAPI_, (EGLenum));
P(EGLContext, eglCreateContext_, (EGLDisplay, EGLConfig, EGLContext, const EGLint *));
P(EGLBoolean, eglMakeCurrent_, (EGLDisplay, EGLSurface, EGLSurface, EGLContext));
P(GLuint, glCreateShader_, (GLenum));
P(void, glShaderSource_, (GLuint, GLsizei, const GLchar *const *, const GLint *));
P(void, glCompileShader_, (GLuint));
P(void, glGetShaderiv_, (GLuint, GLenum, GLint *));
P(void, glGetShaderInfoLog_, (GLuint, GLsizei, GLsizei *, GLchar *));
P(GLuint, glCreateProgram_, (void));
P(void, glAttachShader_, (GLuint, GLuint));
P(void, glLinkProgram_, (GLuint));
P(void, glGetProgramiv_, (GLuint, GLenum, GLint *));
P(void, glGetProgramInfoLog_, (GLuint, GLsizei, GLsizei *, GLchar *));
P(const unsigned char *, glGetString_, (GLenum));

typedef struct { char name[64]; int frag; char *src, *es; GLuint sh; } Sh;
static Sh g_sh[256];
static int g_n;

static int compile(Sh *s)
{
    const char *src = s->es ? s->es : s->src;
    s->sh = glCreateShader_(s->frag ? GL_FRAGMENT_SHADER : GL_VERTEX_SHADER);
    glShaderSource_(s->sh, 1, &src, NULL);
    glCompileShader_(s->sh);
    GLint ok = 0;
    glGetShaderiv_(s->sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[8192];
        log[0] = 0;
        glGetShaderInfoLog_(s->sh, sizeof log, NULL, log);
        printf("FAIL compile %s (%s):\n%s\n", s->name, s->frag ? "frag" : "vert", log);
        if (getenv("SHOW_SRC")) printf("---- source ----\n%s\n----\n", src);
        return 0;
    }
    printf("ok   compile %s\n", s->name);
    return 1;
}

static Sh *find(const char *n)
{
    for (int i = 0; i < g_n; i++) if (!strcmp(g_sh[i].name, n)) return &g_sh[i];
    return NULL;
}

static int link_pair(const char *vs, const char *fs)
{
    Sh *v = find(vs), *f = find(fs);
    if (!v || !f || !v->sh || !f->sh) { printf("skip link %s+%s\n", vs, fs); return 1; }
    GLuint p = glCreateProgram_();
    glAttachShader_(p, v->sh);
    glAttachShader_(p, f->sh);
    glLinkProgram_(p);
    GLint ok = 0;
    glGetProgramiv_(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        log[0] = 0;
        glGetProgramInfoLog_(p, sizeof log, NULL, log);
        printf("FAIL link %s + %s:\n%s\n", vs, fs, log);
        return 0;
    }
    printf("ok   link %s + %s\n", vs, fs);
    return 1;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: check shaders.bin [VS+FS ...]\n"); return 2; }
    void *egl = dlopen("libEGL.so.1", RTLD_NOW);
    void *gles = dlopen("libGLESv2.so.2", RTLD_NOW);
    if (!egl || !gles) { fprintf(stderr, "no libEGL/libGLESv2\n"); return 2; }
    eglGetProcAddress_ = (void *(*)(const char *))dlsym(egl, "eglGetProcAddress");
#define L(lib, v, n) v = (void *)dlsym(lib, n); if (!v) v = eglGetProcAddress_(n)
    L(egl, eglGetPlatformDisplayEXT_, "eglGetPlatformDisplayEXT");
    L(egl, eglInitialize_, "eglInitialize");
    L(egl, eglChooseConfig_, "eglChooseConfig");
    L(egl, eglBindAPI_, "eglBindAPI");
    L(egl, eglCreateContext_, "eglCreateContext");
    L(egl, eglMakeCurrent_, "eglMakeCurrent");
    L(gles, glCreateShader_, "glCreateShader");
    L(gles, glShaderSource_, "glShaderSource");
    L(gles, glCompileShader_, "glCompileShader");
    L(gles, glGetShaderiv_, "glGetShaderiv");
    L(gles, glGetShaderInfoLog_, "glGetShaderInfoLog");
    L(gles, glCreateProgram_, "glCreateProgram");
    L(gles, glAttachShader_, "glAttachShader");
    L(gles, glLinkProgram_, "glLinkProgram");
    L(gles, glGetProgramiv_, "glGetProgramiv");
    L(gles, glGetProgramInfoLog_, "glGetProgramInfoLog");
    L(gles, glGetString_, "glGetString");

    EGLDisplay d = eglGetPlatformDisplayEXT_(EGL_PLATFORM_SURFACELESS_MESA, NULL, NULL);
    EGLint maj, min;
    if (!d || !eglInitialize_(d, &maj, &min)) { fprintf(stderr, "eglInitialize failed\n"); return 2; }
    EGLint cattr[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE };
    EGLConfig cfg;
    EGLint ncfg = 0;
    if (!eglChooseConfig_(d, cattr, &cfg, 1, &ncfg) || ncfg < 1) { fprintf(stderr, "no config\n"); return 2; }
    eglBindAPI_(EGL_OPENGL_ES_API);
    EGLint xattr[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 2, EGL_NONE };
    EGLContext c = eglCreateContext_(d, cfg, NULL, xattr);
    if (!c || !eglMakeCurrent_(d, NULL, NULL, c)) { fprintf(stderr, "context failed\n"); return 2; }
    printf("GL: %s / %s\n", glGetString_(GL_VERSION), glGetString_(GL_RENDERER));
    const char *ext = (const char *)glGetString_(GL_EXTENSIONS);
    printf("GL_EXT_blend_func_extended: %s\n", ext && strstr(ext, "GL_EXT_blend_func_extended") ? "yes" : "NO");

    FILE *fp = fopen(argv[1], "rb");
    if (!fp) { perror(argv[1]); return 2; }
    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *data = malloc((size_t)len + 1);
    fread(data, 1, (size_t)len, fp);
    data[len] = 0;
    fclose(fp);
    for (char *p = data; p < data + len && g_n < 256;) {
        Sh *s = &g_sh[g_n++];
        snprintf(s->name, sizeof s->name, "%s", p); p += strlen(p) + 1;
        s->frag = (*p == 'f'); p += strlen(p) + 1;
        s->src = p; p += strlen(p) + 1;
        s->es = psxak_gles_translate(s->frag ? PSXAK_GLES_FRAGMENT : PSXAK_GLES_VERTEX, s->src);
        if (getenv("DUMP_ES") && s->es) printf("==== %s\n%s\n", s->name, s->es);
    }
    int fails = 0;
    for (int i = 0; i < g_n; i++) fails += !compile(&g_sh[i]);
    for (int i = 2; i < argc; i++) {
        char vs[64], fs[64];
        if (sscanf(argv[i], "%63[^+]+%63s", vs, fs) == 2) fails += !link_pair(vs, fs);
    }
    printf("%d failure(s)\n", fails);
    return fails ? 1 : 0;
}
