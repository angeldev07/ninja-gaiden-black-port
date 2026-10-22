/*
 * kernel.h: xboxkrnl export table (generated in stubs.c) and call dispatch.
 *
 * Xbox code calls the kernel with `call [thunk_slot]`. The loader writes into each
 * slot the address of a generated stub (stubs.c) with the right convention; the stub
 * copies the arguments and calls ngb_kcall(), which logs the call and dispatches it to
 * the implementation registered for that ordinal (impl_*.c).
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
    uint8_t nargs; /* 32-bit arguments; for cdecl (variadic), the ones that get logged */
    const char *name;
    const void *stub; /* NULL for exported variables */
} ngb_kexport_t;

/* stubs.c (generado) */
extern const ngb_kexport_t ngb_kexports[];
extern const size_t ngb_kexport_count;
extern const uint32_t ngb_kexport_max_ordinal;

/* Common point of all stubs. `args` points to nargs dwords (NULL if 0). */
uint64_t ngb_kcall(uint32_t ordinal, uint32_t *args, uint32_t caller);

/* Implementation of an export: receives the arguments as dwords, returns EDX:EAX. */
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
void ngb_kernel_summary(void); /* calls per function, to the log */
unsigned long ngb_kernel_call_seq(void); /* kernel calls so far */

/* data.c: storage for the exported variables */
void *ngb_kdata_address(uint32_t ordinal);
void ngb_kdata_init(const char *xbe_path);
void ngb_kdata_tick(void); /* updates KeTickCount */

/* impl_io.c: 1 if the handle was a synthetic link (not a Windows handle) */
int ngb_io_close(void *h);

/* Each impl_*.c exports its table with this signature; dispatch.c registers them all. */
void ngb_impl_register_mm(void);
void ngb_impl_register_rtl(void);
void ngb_impl_register_ke(void);
void ngb_impl_register_ps(void);
void ngb_impl_register_io(void);
void ngb_impl_register_misc(void);

/* Helpers for the implementations. */
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
