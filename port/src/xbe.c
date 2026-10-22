/*
 * xbe.c: minimal XBE parsing, enough to load it.
 *
 * Header offsets: https://xboxdevwiki.net/Xbe and Cxbx-Reloaded
 * (src/common/xbe/Xbe.h). The retail/debug detection follows ngb_tools.xbe.image:
 * the right key leaves the entry point inside an executable section.
 */
#include "xbe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

const uint8_t *xbe_va_ptr(const xbe_t *xbe, uint32_t va, uint32_t len) {
    uint32_t i;
    /* Headers: VA = base + file offset. */
    if (va >= xbe->base && va - xbe->base + (uint64_t)len <= xbe->size_of_headers &&
        va - xbe->base + (uint64_t)len <= xbe->size)
        return xbe->data + (va - xbe->base);
    for (i = 0; i < xbe->section_count; i++) {
        const xbe_section_t *s = &xbe->sections[i];
        uint32_t backed = s->rsize < s->vsize ? s->rsize : s->vsize;
        if (va >= s->va && (uint64_t)(va - s->va) + len <= backed &&
            (uint64_t)s->raw + (va - s->va) + len <= xbe->size)
            return xbe->data + s->raw + (va - s->va);
    }
    return NULL;
}

static int in_exec_section(const xbe_t *xbe, uint32_t va) {
    uint32_t i;
    for (i = 0; i < xbe->section_count; i++) {
        const xbe_section_t *s = &xbe->sections[i];
        if ((s->flags & XBE_SECTION_EXECUTABLE) && va >= s->va && va - s->va < s->vsize)
            return 1;
    }
    return 0;
}

int xbe_parse(uint8_t *data, size_t size, xbe_t *out, char *err, size_t errlen) {
    uint32_t i, entry_enc, thunk_enc;
    memset(out, 0, sizeof *out);
    out->data = data;
    out->size = size;
    if (size < 0x178 || rd32(data) != XBE_MAGIC) {
        snprintf(err, errlen, "not an XBE (magic or size)");
        return -1;
    }
    out->base = rd32(data + 0x104);
    out->size_of_headers = rd32(data + 0x108);
    out->size_of_image = rd32(data + 0x10C);
    out->size_of_image_header = rd32(data + 0x110);
    out->cert_addr = rd32(data + 0x118);
    out->section_count = rd32(data + 0x11C);
    out->sections_addr = rd32(data + 0x120);
    out->init_flags = rd32(data + 0x124);
    entry_enc = rd32(data + 0x128);
    out->tls_addr = rd32(data + 0x12C);
    out->stack_size = rd32(data + 0x130);
    thunk_enc = rd32(data + 0x158);
    if (out->size_of_headers > size || out->section_count > 256 ||
        out->sections_addr < out->base ||
        out->sections_addr - out->base + (uint64_t)out->section_count * 0x38 >
            out->size_of_headers) {
        snprintf(err, errlen, "inconsistent header (headers 0x%X, %u sections)",
                 out->size_of_headers, out->section_count);
        return -1;
    }
    out->sections = (xbe_section_t *)calloc(out->section_count ? out->section_count : 1,
                                            sizeof(xbe_section_t));
    if (!out->sections) {
        snprintf(err, errlen, "out of memory");
        return -1;
    }
    for (i = 0; i < out->section_count; i++) {
        const uint8_t *h = data + (out->sections_addr - out->base) + i * 0x38;
        xbe_section_t *s = &out->sections[i];
        const uint8_t *name;
        s->flags = rd32(h + 0);
        s->va = rd32(h + 4);
        s->vsize = rd32(h + 8);
        s->raw = rd32(h + 12);
        s->rsize = rd32(h + 16);
        s->name_addr = rd32(h + 20);
        s->header_va = out->sections_addr + i * 0x38;
        if ((uint64_t)s->raw + (s->rsize < s->vsize ? s->rsize : s->vsize) > size ||
            s->va < out->base || (uint64_t)s->va + s->vsize > (uint64_t)out->base + out->size_of_image) {
            snprintf(err, errlen, "section %u outside the file or the image", i);
            return -1;
        }
        name = (s->name_addr >= out->base && s->name_addr - out->base + 8 <= out->size_of_headers)
                   ? data + (s->name_addr - out->base)
                   : NULL;
        if (name) {
            size_t k;
            for (k = 0; k < sizeof s->name - 1 && name[k] && s->name_addr - out->base + k < size; k++)
                s->name[k] = (char)name[k];
        }
    }
    /* Retail or debug: the right key leaves the entry in executable code. */
    out->entry = entry_enc ^ XBE_KEY_ENTRY_RETAIL;
    out->thunk = thunk_enc ^ XBE_KEY_THUNK_RETAIL;
    out->build = "retail";
    if (!in_exec_section(out, out->entry)) {
        out->entry = entry_enc ^ XBE_KEY_ENTRY_DEBUG;
        out->thunk = thunk_enc ^ XBE_KEY_THUNK_DEBUG;
        out->build = "debug";
        if (!in_exec_section(out, out->entry)) {
            snprintf(err, errlen, "no XOR key leaves the entry point in code");
            return -1;
        }
    }
    if (!xbe_va_ptr(out, out->thunk, 4)) {
        snprintf(err, errlen, "kernel thunk 0x%08X outside the file data", out->thunk);
        return -1;
    }
    if (out->tls_addr) {
        const uint8_t *t = xbe_va_ptr(out, out->tls_addr, 0x18);
        if (!t) {
            snprintf(err, errlen, "TLS directory 0x%08X not readable", out->tls_addr);
            return -1;
        }
        out->has_tls = 1;
        out->tls.raw_start = rd32(t + 0);
        out->tls.raw_end = rd32(t + 4);
        out->tls.index_addr = rd32(t + 8);
        out->tls.callbacks_addr = rd32(t + 12);
        out->tls.zero_fill = rd32(t + 16);
        out->tls.characteristics = rd32(t + 20);
    }
    if (out->cert_addr) {
        const uint8_t *c = xbe_va_ptr(out, out->cert_addr, 12);
        if (c)
            out->title_id = rd32(c + 8);
    }
    return 0;
}

int xbe_load_file(const char *path, xbe_t *out, char *err, size_t errlen) {
    FILE *f = NULL;
    long len;
    uint8_t *data;
    if (fopen_s(&f, path, "rb") || !f) {
        snprintf(err, errlen, "cannot open %s", path);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0 || len > 256L * 1024 * 1024) {
        fclose(f);
        snprintf(err, errlen, "invalid file size (%ld)", len);
        return -1;
    }
    data = (uint8_t *)malloc((size_t)len);
    if (!data || fread(data, 1, (size_t)len, f) != (size_t)len) {
        fclose(f);
        free(data);
        snprintf(err, errlen, "error reading %s", path);
        return -1;
    }
    fclose(f);
    return xbe_parse(data, (size_t)len, out, err, errlen);
}

void xbe_free(xbe_t *xbe) {
    free(xbe->sections);
    free(xbe->data);
    memset(xbe, 0, sizeof *xbe);
}
