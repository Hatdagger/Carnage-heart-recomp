/* Desktop GLSL 3.30 -> GLSL ES 3.10 translation (Android GLES renderer).
 *
 * The runtime's OpenGL renderer (psxrecomp/runtime/src/gpu_gl_renderer.c) is
 * written for desktop GL 3.3 core. Android has OpenGL ES 3.2 (Adreno 7xx on
 * the owner's tablet). Rather than change the psxrecomp submodule, the
 * Android build intercepts glShaderSource (psxak_gles.c) and rewrites
 * each "#version 330" shader here:
 *
 *  1. "#version 330" -> "#version 310 es" plus default precision statements
 *     (ES fragment shaders have no default float precision, and no stage has
 *     one for integer samplers).
 *  2. noperspective varyings: GLSL ES has no noperspective qualifier (only the
 *     optional NV extension, which Adreno does not list). The PS1 look depends
 *     on it: textures and Gouraud colour are interpolated linearly in screen
 *     space (affine), and the renderer only uses w != 1 for its opt-in
 *     perspective/PGXP paths. Emulated exactly: the vertex shader multiplies
 *     each such output by gl_Position.w and also outputs w itself
 *     (psxak_npw_); both are interpolated perspective-correctly, and the
 *     fragment shader divides them again, which gives screen-linear values:
 *       (sum l_i a_i w_i / w_i) / (sum l_i w_i / w_i) * ... = sum l_i a_i.
 *     Done by renaming main() to chMn() and adding a main() wrapper.
 *  3. Dual-source blending (the PS1 semi-transparency modes): the fragment
 *     shader's "frag" / "blend_factor" outputs get explicit
 *     layout(location=0, index=0/1) under GL_EXT_blend_func_extended; the
 *     renderer's glBindFragDataLocationIndexed calls become no-ops.
 *  4. A few source patterns that desktop GLSL accepts and GLSL ES does not
 *     (implicit int -> float conversions); see kFixups.
 *
 * Anything else is passed through unchanged; a shader that still fails to
 * compile makes the renderer fall back to software (its all-or-nothing init),
 * and the compile log is in the game log.
 */
#include "psxak_gles_translate.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char *p; size_t n, cap; } Buf;

static void buf_add(Buf *b, const char *s, size_t n)
{
    if (b->n + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 1024;
        while (cap < b->n + n + 1) cap *= 2;
        char *np = (char *)realloc(b->p, cap);
        if (!np) return;
        b->p = np;
        b->cap = cap;
    }
    memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = 0;
}
static void buf_str(Buf *b, const char *s) { buf_add(b, s, strlen(s)); }

static int is_ident(int c) { return isalnum(c) || c == '_'; }

/* Find word w in s at or after s, as a whole identifier. */
static const char *find_word(const char *s, const char *w)
{
    size_t n = strlen(w);
    for (const char *p = strstr(s, w); p; p = strstr(p + 1, w)) {
        int before_ok = (p == s) || !is_ident((unsigned char)p[-1]);
        int after_ok = !is_ident((unsigned char)p[n]);
        if (before_ok && after_ok) return p;
    }
    return NULL;
}

/* Replace every occurrence of `from` with `to`. Returns a new string. */
static char *replace_all(const char *s, const char *from, const char *to)
{
    Buf b = {0};
    size_t fn = strlen(from);
    const char *p = s;
    for (const char *q = strstr(p, from); q; q = strstr(p, from)) {
        buf_add(&b, p, (size_t)(q - p));
        buf_str(&b, to);
        p = q + fn;
    }
    buf_str(&b, p);
    return b.p;
}

/* Desktop-only patterns -> ES equivalents (exact text from the pinned
 * renderer). Kept small and literal on purpose: a pin bump that changes the
 * text just stops matching, and the compile log shows what is new. */
static const char *const kFixups[][2] = {
    { NULL, NULL },
};

#define MAX_NP 32
typedef struct { char type[32]; char name[64]; } NpVar;

/* Parse and rewrite the noperspective declarations. stage decides in/out. */
static char *rewrite_noperspective(int stage, const char *body, NpVar *vars, int *nvars)
{
    Buf b = {0};
    const char *p = body;
    *nvars = 0;
    for (;;) {
        const char *q = find_word(p, "noperspective");
        if (!q) break;
        buf_add(&b, p, (size_t)(q - p));
        const char *s = q + strlen("noperspective");
        while (isspace((unsigned char)*s)) s++;
        int centroid = 0;
        if (!strncmp(s, "centroid", 8) && !is_ident((unsigned char)s[8])) {
            centroid = 1;
            s += 8;
            while (isspace((unsigned char)*s)) s++;
        }
        const char *dir = NULL;
        if (!strncmp(s, "out", 3) && !is_ident((unsigned char)s[3])) { dir = "out"; s += 3; }
        else if (!strncmp(s, "in", 2) && !is_ident((unsigned char)s[2])) { dir = "in"; s += 2; }
        const char *semi = strchr(s, ';');
        if (!dir || !semi) {
            /* Not a declaration we understand: drop just the qualifier. */
            p = q + strlen("noperspective");
            continue;
        }
        while (isspace((unsigned char)*s)) s++;
        char type[32];
        size_t tn = 0;
        while (is_ident((unsigned char)s[tn]) && tn < sizeof type - 1) { type[tn] = s[tn]; tn++; }
        type[tn] = 0;
        s += tn;
        /* names: comma separated identifiers up to ';' */
        char names[MAX_NP][64];
        int nn = 0;
        while (s < semi && nn < MAX_NP) {
            while (s < semi && (isspace((unsigned char)*s) || *s == ',')) s++;
            size_t k = 0;
            while (s < semi && is_ident((unsigned char)*s) && k < 63) names[nn][k++] = *s++;
            names[nn][k] = 0;
            if (k) nn++;
            while (s < semi && *s != ',') s++; /* skip array sizes etc. */
        }
        const char *cq = centroid ? "centroid " : "";
        /* "<dir> T psxak_np_a, psxak_np_b; T a, b;" - the shader body keeps using
         * plain globals; main()'s wrapper moves them in/out. */
        {
            buf_str(&b, cq); buf_str(&b, dir); buf_str(&b, " "); buf_str(&b, type); buf_str(&b, " ");
            for (int i = 0; i < nn; i++) {
                if (i) buf_str(&b, ", ");
                buf_str(&b, "psxak_np_"); buf_str(&b, names[i]);
            }
            buf_str(&b, "; "); buf_str(&b, type); buf_str(&b, " ");
            for (int i = 0; i < nn; i++) {
                if (i) buf_str(&b, ", ");
                buf_str(&b, names[i]);
            }
            buf_str(&b, ";");
        }
        for (int i = 0; i < nn && *nvars < MAX_NP; i++) {
            strncpy(vars[*nvars].type, type, sizeof vars[*nvars].type - 1);
            vars[*nvars].type[sizeof vars[*nvars].type - 1] = 0;
            strncpy(vars[*nvars].name, names[i], sizeof vars[*nvars].name - 1);
            vars[*nvars].name[sizeof vars[*nvars].name - 1] = 0;
            (*nvars)++;
        }
        p = semi + 1;
    }
    buf_str(&b, p);
    return b.p;
}

/* Rename "void main(...)" to "void psxak_main_()". Returns 1 if found. */
static int rename_main(char *s)
{
    for (const char *m = find_word(s, "main"); m; m = find_word(m + 1, "main")) {
        const char *a = m + 4;
        while (isspace((unsigned char)*a)) a++;
        if (*a != '(') continue;
        /* preceded by "void" */
        const char *v = m;
        while (v > s && isspace((unsigned char)v[-1])) v--;
        if (v - s < 4 || strncmp(v - 4, "void", 4) || (v - s > 4 && is_ident((unsigned char)v[-5])))
            continue;
        /* Same-length name, so the rename is in place. */
        memcpy((char *)m, "chMn", 4);
        return 1;
    }
    return 0;
}

char *psxak_gles_translate(int stage, const char *src)
{
    if (!src || strncmp(src, "#version 330", 12) != 0) return NULL;
    const char *body = strchr(src, '\n');
    body = body ? body + 1 : src + strlen(src);

    /* 4. literal fixups */
    char *work = strdup(body);
    if (!work) return NULL;
    for (size_t i = 0; kFixups[i][0]; i++) {
        if (strstr(work, kFixups[i][0])) {
            char *n = replace_all(work, kFixups[i][0], kFixups[i][1]);
            free(work);
            work = n;
        }
    }

    /* 2. noperspective */
    NpVar np[MAX_NP];
    int nnp = 0;
    {
        char *n = rewrite_noperspective(stage, work, np, &nnp);
        free(work);
        work = n;
    }

    /* 3. dual-source outputs */
    int dual = 0;
    if (stage == PSXAK_GLES_FRAGMENT && strstr(work, "out vec4 blend_factor;") &&
        strstr(work, "out vec4 frag;")) {
        char *a = replace_all(work, "out vec4 frag;", "layout(location = 0, index = 0) out vec4 frag;");
        char *b2 = replace_all(a, "out vec4 blend_factor;",
                               "layout(location = 0, index = 1) out vec4 blend_factor;");
        free(a);
        free(work);
        work = b2;
        dual = 1;
    }

    int wrap_main = nnp > 0 && rename_main(work);

    Buf o = {0};
    buf_str(&o, "#version 310 es\n");
    if (dual) buf_str(&o, "#extension GL_EXT_blend_func_extended : require\n");
    buf_str(&o,
        "precision highp float;\nprecision highp int;\n"
        "precision highp sampler2D;\nprecision highp usampler2D;\n"
        "precision highp isampler2D;\nprecision highp sampler2DArray;\n"
        "precision highp sampler3D;\nprecision highp samplerCube;\n");
    if (wrap_main)
        buf_str(&o, stage == PSXAK_GLES_VERTEX ? "out float psxak_npw_;\n" : "in float psxak_npw_;\n");
    buf_str(&o, work);
    free(work);
    if (wrap_main) {
        buf_str(&o, "\nvoid main(){\n");
        if (stage == PSXAK_GLES_VERTEX) {
            buf_str(&o, "  chMn();\n");
            for (int i = 0; i < nnp; i++) {
                buf_str(&o, "  psxak_np_"); buf_str(&o, np[i].name); buf_str(&o, " = ");
                buf_str(&o, np[i].name); buf_str(&o, " * gl_Position.w;\n");
            }
            buf_str(&o, "  psxak_npw_ = gl_Position.w;\n");
        } else {
            for (int i = 0; i < nnp; i++) {
                buf_str(&o, "  "); buf_str(&o, np[i].name); buf_str(&o, " = psxak_np_");
                buf_str(&o, np[i].name); buf_str(&o, " / psxak_npw_;\n");
            }
            buf_str(&o, "  chMn();\n");
        }
        buf_str(&o, "}\n");
    }
    return o.p;
}
