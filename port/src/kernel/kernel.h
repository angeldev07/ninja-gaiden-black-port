/*
 * kernel.h: tabla de exports de xboxkrnl (generada en stubs.c) y despacho de llamadas.
 *
 * El código Xbox llama al kernel con `call [slot_del_thunk]`. El loader escribe en cada
 * slot la dirección de un stub generado (stubs.c) con la convención correcta; el stub
 * copia los argumentos y llama a ngb_kcall(), que registra la llamada y la despacha a
 * la implementación registrada para ese ordinal (impl_*.c).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define NGB_KIND_FUNC 0
#define NGB_KIND_DATA 1

#define NGB_CONV_STDCALL 0
#define NGB_CONV_FASTCALL 1
#define NGB_CONV_CDECL 2
#define NGB_CONV_NONE 3

#define NGB_MAX_ORDINAL 512

typedef struct ngb_kexport {
    uint16_t ordinal;
    uint8_t kind;  /* NGB_KIND_* */
    uint8_t conv;  /* NGB_CONV_* */
    uint8_t nargs; /* argumentos de 32 bits; en cdecl (variádicas), los que se registran */
    const char *name;
    const void *stub; /* NULL en las variables exportadas */
} ngb_kexport_t;

/* stubs.c (generado) */
extern const ngb_kexport_t ngb_kexports[];
extern const size_t ngb_kexport_count;
extern const uint32_t ngb_kexport_max_ordinal;

/* Punto común de todos los stubs. `args` apunta a nargs dwords (NULL si 0). */
uint64_t ngb_kcall(uint32_t ordinal, uint32_t *args, uint32_t caller);

/* Implementación de un export: recibe los argumentos como dwords, devuelve EDX:EAX. */
typedef uint64_t (*ngb_kimpl_fn)(uint32_t *args);

typedef struct ngb_kimpl {
    uint16_t ordinal;
    ngb_kimpl_fn fn;
} ngb_kimpl_t;

/* dispatch.c */
const ngb_kexport_t *ngb_kexport(uint32_t ordinal);
void ngb_kernel_register(const ngb_kimpl_t *impls, size_t count);
void ngb_kernel_init(void);
size_t ngb_kernel_impl_count(void);
int ngb_kernel_is_implemented(uint32_t ordinal);
void ngb_kernel_summary(void); /* llamadas por función, al log */

/* data.c: almacenamiento de las variables exportadas */
void *ngb_kdata_address(uint32_t ordinal);
void ngb_kdata_init(const char *xbe_path);
void ngb_kdata_tick(void); /* actualiza KeTickCount */

/* impl_io.c: 1 si el handle era un enlace sintético (no es un handle de Windows) */
int ngb_io_close(void *h);

/* Cada impl_*.c exporta su tabla con esta firma; dispatch.c las registra todas. */
void ngb_impl_register_mm(void);
void ngb_impl_register_rtl(void);
void ngb_impl_register_ke(void);
void ngb_impl_register_ps(void);
void ngb_impl_register_io(void);
void ngb_impl_register_misc(void);

/* Ayudas para las implementaciones. */
#define ARG(i) (args[(i)])
#define PTR(i) ((void *)(uintptr_t)args[(i)])
#define RET32(v) ((uint64_t)(uint32_t)(v))

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS 0x00000000u
#endif
#ifndef STATUS_PENDING
#define STATUS_PENDING 0x00000103u
#endif
#ifndef STATUS_TIMEOUT
#define STATUS_TIMEOUT 0x00000102u
#endif
#ifndef STATUS_UNSUCCESSFUL
#define STATUS_UNSUCCESSFUL 0xC0000001u
#endif
#ifndef STATUS_NOT_IMPLEMENTED
#define STATUS_NOT_IMPLEMENTED 0xC0000002u
#endif
#ifndef STATUS_INVALID_HANDLE
#define STATUS_INVALID_HANDLE 0xC0000008u
#endif
#ifndef STATUS_INVALID_PARAMETER
#define STATUS_INVALID_PARAMETER 0xC000000Du
#endif
#ifndef STATUS_NO_MEMORY
#define STATUS_NO_MEMORY 0xC0000017u
#endif
#ifndef STATUS_OBJECT_NAME_NOT_FOUND
#define STATUS_OBJECT_NAME_NOT_FOUND 0xC0000034u
#endif
#ifndef STATUS_OBJECT_PATH_NOT_FOUND
#define STATUS_OBJECT_PATH_NOT_FOUND 0xC000003Au
#endif
#ifndef STATUS_OBJECT_NAME_COLLISION
#define STATUS_OBJECT_NAME_COLLISION 0xC0000035u
#endif
#ifndef STATUS_NO_SUCH_FILE
#define STATUS_NO_SUCH_FILE 0xC000000Fu
#endif
#ifndef STATUS_END_OF_FILE
#define STATUS_END_OF_FILE 0xC0000011u
#endif
#ifndef STATUS_NO_MORE_FILES
#define STATUS_NO_MORE_FILES 0x80000006u
#endif
#ifndef STATUS_ACCESS_DENIED
#define STATUS_ACCESS_DENIED 0xC0000022u
#endif
#ifndef STATUS_NOT_SUPPORTED
#define STATUS_NOT_SUPPORTED 0xC00000BBu
#endif
#ifndef STATUS_UNRECOGNIZED_VOLUME
#define STATUS_UNRECOGNIZED_VOLUME 0xC000014Fu
#endif
