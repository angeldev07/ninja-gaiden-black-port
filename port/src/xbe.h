/*
 * xbe.h: minimal XBE parsing (header, sections, TLS) and un-XOR of the entry point
 * and the kernel thunk. Equivalent to the loading part of ngb_tools.xbe.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define XBE_MAGIC 0x48454258u /* "XBEH" */
#define XBE_KEY_ENTRY_RETAIL 0xA8FC57ABu
#define XBE_KEY_THUNK_RETAIL 0x5B6D40B6u
#define XBE_KEY_ENTRY_DEBUG 0x94859D4Bu
#define XBE_KEY_THUNK_DEBUG 0xEFB1F152u
#define XBE_SECTION_EXECUTABLE 0x04u
#define XBE_SECTION_INSERTED_FILE 0x08u

typedef struct xbe_section {
    uint32_t flags, va, vsize, raw, rsize, name_addr;
    uint32_t header_va; /* VA of its section header (what XeLoadSection receives) */
    char name[16];
} xbe_section_t;

typedef struct xbe_tls {
    uint32_t raw_start, raw_end, index_addr, callbacks_addr, zero_fill, characteristics;
} xbe_tls_t;

typedef struct xbe {
    uint8_t *data;
    size_t size;
    uint32_t base, size_of_headers, size_of_image, size_of_image_header;
    uint32_t cert_addr, section_count, sections_addr, init_flags;
    uint32_t entry, thunk, tls_addr, stack_size;
    const char *build; /* "retail" or "debug" depending on the valid XOR key */
    xbe_section_t *sections;
    int has_tls;
    xbe_tls_t tls;
    uint32_t title_id;
} xbe_t;

/* Takes ownership of `data` (malloc). Returns 0 on success; otherwise leaves the reason in err. */
int xbe_parse(uint8_t *data, size_t size, xbe_t *out, char *err, size_t errlen);
int xbe_load_file(const char *path, xbe_t *out, char *err, size_t errlen);
/* Pointer to the file bytes that back [va, va+len), or NULL. */
const uint8_t *xbe_va_ptr(const xbe_t *xbe, uint32_t va, uint32_t len);
void xbe_free(xbe_t *xbe);
