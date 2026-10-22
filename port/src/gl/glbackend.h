/*
 * glbackend.h: OpenGL 4.x backend for the port's render (H7b, ADR-06).
 *
 * Creates a core 4.x context with WGL, on its own hidden window or on an HWND passed by the
 * caller (the presenter), and always draws into its own FBO (720x480 by default, the Xbox
 * mode) from which the pixels can be read. The shaders are the GLSL ones from
 * port/src/shaders (vsh.h and psh.h), which name their inputs `v0`-`v15` with
 * `layout(location = i)`: the vertex format says which attribute feeds each `v<i>`.
 *
 * Everything is single-threaded: the context's thread. Functions return 0 on success or a
 * negative `glb_error`.
 */
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stddef.h>
#include <stdint.h>

#include "texconv.h"

#define GLB_DEFAULT_WIDTH 720
#define GLB_DEFAULT_HEIGHT 480
#define GLB_MAX_INPUTS 16
#define GLB_MAX_TEXTURES 4

enum glb_error {
    GLB_ERR_ARGS = -1,      /* invalid arguments or backend not initialized */
    GLB_ERR_WINDOW = -2,    /* could not create the window or the pixel format */
    GLB_ERR_CONTEXT = -3,   /* no 4.x context (driver without WGL_ARB_create_context) */
    GLB_ERR_FUNCTIONS = -4, /* a GL 4.x function is missing */
    GLB_ERR_FBO = -5,       /* the framebuffer is not complete */
    GLB_ERR_COMPILE = -6,   /* the driver rejected a shader or the program link */
    GLB_ERR_GL = -7,        /* glGetError after an operation */
};

/* What the driver reports. */
typedef struct glb_info {
    char vendor[128];
    char renderer[128];
    char version[128];
    char glsl_version[64];
    int major, minor;
} glb_info;

/*
 * Attribute type: those of the NV2A SET_VERTEX_DATA_ARRAY_FORMAT (bits 0-3).
 * Source: xemu, nv2a_regs.h (NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_*) and pgraph/gl/vertex.c.
 */
typedef enum glb_attr_type {
    GLB_ATTR_FLOAT = 0, /* F (2): float */
    GLB_ATTR_UB_D3D,    /* UB_D3D (0): D3DCOLOR, B G R A in memory, normalized (always 4) */
    GLB_ATTR_UB_OGL,    /* UB_OGL (4): normalized R G B A bytes */
    GLB_ATTR_S1,        /* S1 (1): signed short normalized to [-1, 1] */
    GLB_ATTR_S32K,      /* S32K (5): unnormalized short */
    GLB_ATTR_CMP,       /* CMP (6): signed 11:11:10 normal in a u32 (converted on the CPU) */
} glb_attr_type;

/*
 * An attribute of `components` components (1-4; CMP always 3) at `offset` bytes; feeds
 * v<input>. `stride` 0 = the format's. `type` 0 = float, so the three-field initializers
 * remain valid.
 */
typedef struct glb_attrib {
    int input;
    int components;
    int offset;
    int type;   /* glb_attr_type */
    int stride; /* bytes between vertices of this attribute; 0 = glb_format.stride */
} glb_attrib;

typedef struct glb_format {
    int stride; /* bytes per vertex */
    int n_attribs;
    glb_attrib attribs[GLB_MAX_INPUTS];
} glb_format;

typedef enum glb_prim {
    GLB_POINTS,
    GLB_LINES,
    GLB_LINE_STRIP,
    GLB_TRIANGLES,
    GLB_TRIANGLE_STRIP,
    GLB_TRIANGLE_FAN,
    GLB_LINE_LOOP,
    GLB_QUADS,      /* split into triangles (0 1 2, 0 2 3): the core profile has no quads */
    GLB_QUAD_STRIP, /* same vertex order as a triangle strip */
    GLB_POLYGON,    /* convex: triangle fan */
} glb_prim;

typedef enum glb_uniform_kind {
    GLB_U_FLOAT, /* `count` floats */
    GLB_U_VEC2,  /* `count` vec2 */
    GLB_U_VEC4,  /* `count` vec4 */
    GLB_U_MAT2,  /* `count` mat2 (column-major) */
    GLB_U_INT,   /* one int (samplers: the texture unit) */
} glb_uniform_kind;

typedef struct glb_uniform {
    const char *name;  /* "c", "surfaceSize", "tex0"...; ignored if it does not exist */
    glb_uniform_kind kind;
    int count;
    const float *values;
    int ivalue;
} glb_uniform;

/*
 * Creates the context and the FBO of `width` x `height` (0 = 720x480). With `hwnd` NULL it uses
 * its own hidden window; with an HWND, it sets a pixel format on it and draws on its DC
 * (glb_present copies the FBO to that window).
 */
int glb_init(HWND hwnd, int width, int height);

/* DC of the context's window (for SwapBuffers from whoever draws). */
HDC glb_get_dc(void);

/* Driver information; NULL if not initialized. */
const glb_info *glb_get_info(void);

/*
 * Compiles and links a program. On failure, returns GLB_ERR_COMPILE and leaves in `log` what
 * the driver said (always NUL-terminated). `fs` may be NULL to compile only the VS.
 */
int glb_compile(const char *vs, const char *fs, unsigned *program, char *log, size_t loglen);

/* Compiles a single stage (1 = fragment, 0 = vertex), without linking: validates a standalone shader. */
int glb_compile_stage(int fragment, const char *source, char *log, size_t loglen);

void glb_delete_program(unsigned program);

/* Uploads an RGBA8 texture (rows top to bottom) to unit `unit` (0-3). */
int glb_set_texture(int unit, int width, int height, const uint8_t *rgba, int linear);

/* Clears the whole FBO: removes the scissor and opens the color, depth and stencil masks. */
int glb_clear(float r, float g, float b, float a);

/*
 * Draws `count` vertices from `vertices` (format `fmt`) with the given program and uniforms.
 * The v<i> inputs the format does not provide are left at (0, 0, 0, 1).
 */
int glb_draw(unsigned program, const void *vertices, const glb_format *fmt, glb_prim prim,
             int count, const glb_uniform *uniforms, int n_uniforms);

/* A complete draw call (glb_draw is the case without indices). */
typedef struct glb_draw_call {
    unsigned program;
    const void *vertices;
    size_t vertex_bytes;     /* the whole vertex buffer */
    const glb_format *format;
    glb_prim prim;
    int count;               /* vertices, or indices if `indices` is not NULL */
    const void *indices;     /* NULL = no indices */
    int index_size;          /* 2 (u16, ARRAY_ELEMENT16) or 4 (u32, ARRAY_ELEMENT32) */
    const glb_uniform *uniforms;
    int n_uniforms;
} glb_draw_call;

/* Draws with optional indices, attributes of any glb_attr_type and quads. */
int glb_draw_ex(const glb_draw_call *call);

/*
 * Attribute `input` from SET_VERTEX_DATA_ARRAY_FORMAT (type in bits 0-3, component count
 * in 4-7, stride in 8-31) and the offset of its data inside the buffer.
 * Returns 0, 1 if the attribute is disabled (0 components) or GLB_ERR_ARGS.
 */
int glb_attrib_from_nv2a(int input, uint32_t array_format, uint32_t offset, glb_attrib *out);

/* SET_BEGIN_END primitive (1 POINTS ... 10 POLYGON). 0 on success, GLB_ERR_ARGS otherwise. */
int glb_prim_from_nv2a(uint32_t op, glb_prim *out);

/* Signed CMP 11:11:10 normal to three floats in [-1, 1]. */
void glb_decode_cmp(uint32_t packed, float out[3]);

/* 1 if the driver has GL_EXT_texture_compression_s3tc (for texconv_describe). */
int glb_has_s3tc(void);

/*
 * Uploads to unit `unit` a texture converted with texconv (all faces and levels):
 * 2D or cube map, compressed or not. `linear` picks the filter (with mipmaps if there are levels).
 */
int glb_upload_texture(int unit, const texconv_desc *desc, const uint8_t *data, int linear);

/* Reads the FBO: width * height * 4 bytes RGBA, rows top to bottom. */
int glb_read_rgba(uint8_t *buf, size_t buflen);

/* Copies the FBO to the window (its own, if shown, or the caller's) and swaps. */
int glb_present(void);

/* Shows or hides the own window (does nothing with an external HWND). */
void glb_show_window(int show);

void glb_shutdown(void);

const char *glb_error_text(int code);
