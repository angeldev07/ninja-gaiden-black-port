/*
 * glbackend.h: backend de OpenGL 4.x para el render del port (H7b, ADR-06).
 *
 * Crea un contexto core 4.x con WGL, sobre una ventana oculta propia o sobre un HWND que
 * pase el llamante (el presentador), y dibuja siempre en un FBO propio (por defecto
 * 720x480, el modo de la Xbox) del que se pueden leer los píxeles. Los shaders son los
 * GLSL de port/src/shaders (vsh.h y psh.h), que nombran sus entradas `v0`-`v15` con
 * `layout(location = i)`: el formato de vértice dice qué atributo alimenta cada `v<i>`.
 *
 * Todo es de un solo hilo: el del contexto. Las funciones devuelven 0 si van bien o un
 * `glb_error` negativo.
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
    GLB_ERR_ARGS = -1,      /* argumentos inválidos o backend sin iniciar */
    GLB_ERR_WINDOW = -2,    /* no se pudo crear la ventana o el formato de píxel */
    GLB_ERR_CONTEXT = -3,   /* no hay contexto 4.x (driver sin WGL_ARB_create_context) */
    GLB_ERR_FUNCTIONS = -4, /* falta alguna función de GL 4.x */
    GLB_ERR_FBO = -5,       /* el framebuffer no está completo */
    GLB_ERR_COMPILE = -6,   /* el driver rechazó un shader o el enlace del program */
    GLB_ERR_GL = -7,        /* glGetError tras una operación */
};

/* Lo que dice el driver. */
typedef struct glb_info {
    char vendor[128];
    char renderer[128];
    char version[128];
    char glsl_version[64];
    int major, minor;
} glb_info;

/*
 * Tipo de un atributo: los de SET_VERTEX_DATA_ARRAY_FORMAT (bits 0-3) del NV2A.
 * Fuente: xemu, nv2a_regs.h (NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_*) y pgraph/gl/vertex.c.
 */
typedef enum glb_attr_type {
    GLB_ATTR_FLOAT = 0, /* F (2): float */
    GLB_ATTR_UB_D3D,    /* UB_D3D (0): D3DCOLOR, B G R A en memoria, normalizado (siempre 4) */
    GLB_ATTR_UB_OGL,    /* UB_OGL (4): bytes R G B A normalizados */
    GLB_ATTR_S1,        /* S1 (1): short con signo normalizado a [-1, 1] */
    GLB_ATTR_S32K,      /* S32K (5): short sin normalizar */
    GLB_ATTR_CMP,       /* CMP (6): normal 11:11:10 con signo en un u32 (se convierte en CPU) */
} glb_attr_type;

/*
 * Un atributo de `components` componentes (1-4; CMP siempre 3) en `offset` bytes; alimenta
 * v<input>. `stride` 0 = el del formato. `type` 0 = float, así que los inicializadores de
 * tres campos siguen valiendo.
 */
typedef struct glb_attrib {
    int input;
    int components;
    int offset;
    int type;   /* glb_attr_type */
    int stride; /* bytes entre vértices de este atributo; 0 = glb_format.stride */
} glb_attrib;

typedef struct glb_format {
    int stride; /* bytes por vértice */
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
    GLB_QUADS,      /* se parten en triángulos (0 1 2, 0 2 3): el core profile no tiene quads */
    GLB_QUAD_STRIP, /* mismo orden de vértices que una tira de triángulos */
    GLB_POLYGON,    /* convexo: abanico de triángulos */
} glb_prim;

typedef enum glb_uniform_kind {
    GLB_U_FLOAT, /* `count` floats */
    GLB_U_VEC2,  /* `count` vec2 */
    GLB_U_VEC4,  /* `count` vec4 */
    GLB_U_MAT2,  /* `count` mat2 (por columnas) */
    GLB_U_INT,   /* un int (samplers: la unidad de textura) */
} glb_uniform_kind;

typedef struct glb_uniform {
    const char *name;  /* "c", "surfaceSize", "tex0"...; si no existe se ignora */
    glb_uniform_kind kind;
    int count;
    const float *values;
    int ivalue;
} glb_uniform;

/*
 * Crea el contexto y el FBO de `width` x `height` (0 = 720x480). Con `hwnd` NULL usa una
 * ventana oculta propia; con un HWND, le pone un formato de píxel y dibuja sobre su DC
 * (glb_present copia el FBO a esa ventana).
 */
int glb_init(HWND hwnd, int width, int height);

/* Información del driver; NULL si no se ha iniciado. */
const glb_info *glb_get_info(void);

/*
 * Compila y enlaza un program. Si falla, devuelve GLB_ERR_COMPILE y deja en `log` lo que
 * dijo el driver (siempre terminado en NUL). `fs` puede ser NULL para compilar solo el VS.
 */
int glb_compile(const char *vs, const char *fs, unsigned *program, char *log, size_t loglen);

/* Compila una sola etapa (1 = fragment, 0 = vertex), sin enlazar: valida un shader suelto. */
int glb_compile_stage(int fragment, const char *source, char *log, size_t loglen);

void glb_delete_program(unsigned program);

/* Sube una textura RGBA8 (filas de arriba abajo) a la unidad `unit` (0-3). */
int glb_set_texture(int unit, int width, int height, const uint8_t *rgba, int linear);

/* Limpia el FBO entero: quita el scissor y abre las máscaras de color, depth y stencil. */
int glb_clear(float r, float g, float b, float a);

/*
 * Dibuja `count` vértices de `vertices` (formato `fmt`) con el program y los uniforms dados.
 * Las entradas v<i> que el formato no da quedan a (0, 0, 0, 1).
 */
int glb_draw(unsigned program, const void *vertices, const glb_format *fmt, glb_prim prim,
             int count, const glb_uniform *uniforms, int n_uniforms);

/* Una llamada de dibujo completa (glb_draw es el caso sin índices). */
typedef struct glb_draw_call {
    unsigned program;
    const void *vertices;
    size_t vertex_bytes;     /* todo el buffer de vértices */
    const glb_format *format;
    glb_prim prim;
    int count;               /* vértices, o índices si `indices` no es NULL */
    const void *indices;     /* NULL = sin índices */
    int index_size;          /* 2 (u16, ARRAY_ELEMENT16) o 4 (u32, ARRAY_ELEMENT32) */
    const glb_uniform *uniforms;
    int n_uniforms;
} glb_draw_call;

/* Dibuja con índices opcionales, atributos de cualquier glb_attr_type y quads. */
int glb_draw_ex(const glb_draw_call *call);

/*
 * Atributo `input` a partir de SET_VERTEX_DATA_ARRAY_FORMAT (tipo en los bits 0-3, número de
 * componentes en 4-7, stride en 8-31) y del offset de sus datos dentro del buffer.
 * Devuelve 0, 1 si el atributo está desactivado (0 componentes) o GLB_ERR_ARGS.
 */
int glb_attrib_from_nv2a(int input, uint32_t array_format, uint32_t offset, glb_attrib *out);

/* Primitiva de SET_BEGIN_END (1 POINTS ... 10 POLYGON). 0 si va bien, GLB_ERR_ARGS si no. */
int glb_prim_from_nv2a(uint32_t op, glb_prim *out);

/* Normal CMP 11:11:10 con signo a tres floats en [-1, 1]. */
void glb_decode_cmp(uint32_t packed, float out[3]);

/* 1 si el driver tiene GL_EXT_texture_compression_s3tc (para texconv_describe). */
int glb_has_s3tc(void);

/*
 * Sube a la unidad `unit` una textura convertida con texconv (todas las caras y niveles):
 * 2D o cube map, comprimida o no. `linear` elige el filtro (con mipmaps si hay niveles).
 */
int glb_upload_texture(int unit, const texconv_desc *desc, const uint8_t *data, int linear);

/* Lee el FBO: width * height * 4 bytes RGBA, filas de arriba abajo. */
int glb_read_rgba(uint8_t *buf, size_t buflen);

/* Copia el FBO a la ventana (la propia, si se muestra, o la del llamante) y hace swap. */
int glb_present(void);

/* Muestra u oculta la ventana propia (no hace nada con un HWND externo). */
void glb_show_window(int show);

void glb_shutdown(void);

const char *glb_error_text(int code);
