/*
 * rcache.h: cachés puras del render OpenGL de PGRAPH (H7c). Sin GL: guardan los nombres de
 * los objetos GL como números opacos y deciden qué crear, reutilizar o borrar, para que la
 * lógica se pueda probar sin contexto (tests/rcache_test.c).
 *
 * - Superficies (render targets): una entrada por (dirección física, tipo color/zeta). El
 *   juego dibuja en una superficie y luego la usa como textura (render-to-texture): la
 *   textura se resuelve a la superficie por su dirección, sin pasar por la CPU. Una
 *   superficie nueva que pisa la memoria de otra la invalida.
 * - Texturas: una entrada por (dirección, formato, control1, image_rect, paleta), con el
 *   hash del contenido. La primera vez que se usa en un frame se vuelve a hashear la
 *   memoria; solo si cambió se vuelve a convertir y subir (invalidación por frame).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define RC_MAX_SURFACES 64
#define RC_MAX_TEXTURES 512

enum rc_kind { RC_COLOR = 0, RC_ZETA = 1 };

typedef struct rc_surface {
    int used;
    int kind;               /* rc_kind */
    uint32_t addr;          /* física del primer píxel */
    uint32_t bytes;         /* memoria que ocupa en la Xbox: pitch x alto */
    uint32_t width, height; /* en píxeles */
    uint32_t format;        /* SET_SURFACE_FORMAT: código de color o de zeta */
    unsigned gl_tex;        /* textura GL (RGBA8 o DEPTH24_STENCIL8); 0 = por crear */
    uint32_t last_use;      /* reloj de la caché, para el LRU */
    uint64_t mem_sig;       /* rc_sparse_hash de la RAM cuando se usó como destino: si la CPU
                             * escribe ahí otra cosa (una textura nueva), deja de coincidir */
} rc_surface;

typedef struct rc_surfaces {
    rc_surface s[RC_MAX_SURFACES];
    uint32_t clock;
} rc_surfaces;

/* Resultado de rc_surface_get. */
enum rc_surface_result {
    RC_SURF_HIT = 0,     /* existía con el mismo tamaño y formato */
    RC_SURF_NEW = 1,     /* entrada nueva: hay que crear la textura GL */
    RC_SURF_RESIZED = 2, /* existía con otro tamaño o formato: borrar `gl_tex` viejo y crear */
};

/*
 * Busca o crea la superficie (addr, kind). Las texturas GL que hay que borrar (entradas
 * expulsadas por LRU, pisadas por la nueva o recreadas) se dejan en `dead` (hasta
 * `dead_cap`, el resto se pierde) y su número en `*n_dead`. NULL si los argumentos no
 * valen (tamaño 0 o mayor de 4096).
 */
rc_surface *rc_surface_get(rc_surfaces *c, int kind, uint32_t addr, uint32_t pitch,
                           uint32_t width, uint32_t height, uint32_t format, int *result,
                           unsigned *dead, int dead_cap, int *n_dead);

/* Superficie de color que empieza exactamente en `addr`, o NULL (para resolver texturas). */
rc_surface *rc_surface_find(rc_surfaces *c, int kind, uint32_t addr);

typedef struct rc_texkey {
    uint32_t addr, format, control1, rect, palette;
} rc_texkey;

typedef struct rc_texture {
    int used;
    rc_texkey key;
    uint64_t hash;          /* del contenido (y de la paleta) en la última subida */
    uint32_t checked_frame; /* frame en el que se comprobó el hash por última vez */
    unsigned gl_tex;
    uint32_t last_use;
} rc_texture;

typedef struct rc_textures {
    rc_texture t[RC_MAX_TEXTURES];
    uint32_t clock;
    uint32_t hits, uploads, checks; /* estadísticas */
} rc_textures;

/* Qué tiene que hacer el llamante con la entrada que devuelve rc_texture_get. */
enum rc_texture_action {
    RC_TEX_USE = 0,    /* ya comprobada en este frame: usar gl_tex tal cual */
    RC_TEX_CHECK = 1,  /* hashear el contenido y llamar a rc_texture_checked */
    RC_TEX_UPLOAD = 2, /* entrada nueva: crear gl_tex, subir y llamar a rc_texture_uploaded */
};

/* Busca o crea la entrada de `key`. Si expulsa una por LRU, su textura GL va a `*dead`
 * (0 si no hay). */
rc_texture *rc_texture_get(rc_textures *c, const rc_texkey *key, uint32_t frame, int *action,
                           unsigned *dead);

/* Tras hashear (RC_TEX_CHECK): 1 si el contenido cambió y hay que volver a subirlo. */
int rc_texture_checked(rc_textures *c, rc_texture *t, uint64_t hash, uint32_t frame);

/* Tras subir: guarda el hash y el frame. */
void rc_texture_uploaded(rc_texture *t, uint64_t hash, uint32_t frame);

/* Hash rápido del contenido (palabras de 32 bits; la cola, byte a byte). */
uint64_t rc_hash(const void *data, size_t len, uint64_t seed);

/* Firma barata de una zona grande: rc_hash de RC_SPARSE_SAMPLES trozos de 16 bytes
 * repartidos por igual (lee unos 4 KB aunque la zona tenga megas). */
#define RC_SPARSE_SAMPLES 256
uint64_t rc_sparse_hash(const void *data, size_t len);

/* Quita la superficie de la caché (la CPU ha reutilizado su memoria); devuelve su textura GL
 * para que el llamante la borre. */
unsigned rc_surface_drop(rc_surface *s);

/* ---- utilidades puras del dibujo ---- */

/* Un atributo según SET_VERTEX_DATA_ARRAY_FORMAT (tipo en bits 0-3, tamaño en 4-7, stride en
 * 8-31). Tipos del NV2A: 0 UB_D3D, 1 S1, 2 F, 4 UB_OGL, 5 S32K, 6 CMP. */
typedef struct rc_attr {
    int enabled;
    int type;        /* el del NV2A */
    int comps;       /* componentes que ve el shader (CMP = 3) */
    int src_bytes;   /* bytes que ocupa un elemento en memoria */
    int dst_bytes;   /* bytes que ocupa en el VBO (CMP se expande a 3 floats) */
    int inline_words;/* palabras de 32 bits en INLINE_ARRAY */
    uint32_t stride; /* bytes entre vértices en un array */
} rc_attr;

/* 0 si el formato es válido (también si está desactivado: enabled = 0), -1 si no. */
int rc_attr_layout(uint32_t array_format, rc_attr *out);

/* QUADS -> TRIANGLES: (a b c d) -> (a b c, a c d). `out` necesita n / 4 * 6 entradas.
 * Devuelve el número de índices escritos. */
uint32_t rc_quads_to_triangles(const uint32_t *in, uint32_t n, uint32_t *out);
