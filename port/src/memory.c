/*
 * memory.c: espacio de direcciones Xbox dentro del proceso Win32.
 *
 * - Imagen del XBE: el exe (loader_stub.c) se enlaza en 0x10000 con /FIXED y una sección
 *   BSS gigante, así que el cargador de Windows ya reservó [0x10000, NGB_RESERVE_END)
 *   al crear el proceso, antes de que heaps, pilas o mapeos de NLS ocupen la zona baja.
 *   Aquí solo se cambia la protección a RWX y se copian cabeceras y secciones.
 * - Memoria contigua: arena de 64 MB en 0x80000000 (el exe es LARGEADDRESSAWARE), como
 *   la ventana física de la Xbox; MmGetPhysicalAddress = va - 0x80000000.
 */
#include "ngb.h"

#include <stdio.h>
#include <string.h>

#include "kernel/kernel.h"

#define MAX_CONTIG_BLOCKS 4096

typedef struct contig_block {
    uint32_t base, size;
    int used;
} contig_block_t;

static uint8_t *g_contig;
static uint32_t g_contig_top; /* offset del siguiente bloque libre */
static contig_block_t g_blocks[MAX_CONTIG_BLOCKS];
static int g_nblocks;
static CRITICAL_SECTION g_contig_lock;

/*
 * Imagen del kernel falsa en 0x80010000. XAPI lee la cabecera PE de xboxkrnl.exe (que en
 * Xbox está en 0x80010000) para buscar su sección INIT. Se deja una cabecera PE mínima
 * con una sola sección ".text": no hay INIT y XAPI se salta ese paso. El primer MB de la
 * arena contigua queda reservado para esto (como la RAM baja que ocupa el kernel real).
 */
#define NGB_KERNEL_IMAGE 0x80010000u
#define NGB_CONTIG_FIRST 0x00100000u

static void fake_kernel_image(void) {
    uint8_t *k;
    if ((uint32_t)(uintptr_t)g_contig != NGB_CONTIG_BASE)
        return;
    k = (uint8_t *)VirtualAlloc(g_contig, NGB_CONTIG_FIRST, MEM_COMMIT, PAGE_READWRITE);
    if (!k)
        return;
    k = (uint8_t *)(uintptr_t)NGB_KERNEL_IMAGE;
    k[0] = 'M';
    k[1] = 'Z';
    *(uint32_t *)(k + 0x3C) = 0x80;
    memcpy(k + 0x80, "PE\0\0", 4);
    *(uint16_t *)(k + 0x80 + 4) = 0x14C;       /* i386 */
    *(uint16_t *)(k + 0x80 + 6) = 1;           /* NumberOfSections */
    *(uint16_t *)(k + 0x80 + 0x14) = 0xE0;     /* SizeOfOptionalHeader */
    *(uint16_t *)(k + 0x80 + 0x18) = 0x10B;    /* PE32 */
    *(uint32_t *)(k + 0x80 + 0x18 + 0x1C) = NGB_KERNEL_IMAGE; /* ImageBase */
    memcpy(k + 0x80 + 0x18 + 0xE0, ".text", 5);
    g_contig_top = NGB_CONTIG_FIRST;
    ngb_log("memoria: cabecera PE falsa de xboxkrnl en 0x%08X (sin sección INIT)", NGB_KERNEL_IMAGE);
}

/* Toda la zona [0x10000, sombra + 4 KB) tiene que ser la imagen del exe stub. */
static int check_reserve(void) {
    uint32_t a = NGB_RESERVE_BASE;
    while (a < NGB_PE_SHADOW + 0x1000u) {
        MEMORY_BASIC_INFORMATION m;
        if (!VirtualQuery((void *)(uintptr_t)a, &m, sizeof m) || m.Type != MEM_IMAGE ||
            (uint32_t)(uintptr_t)m.AllocationBase != NGB_RESERVE_BASE) {
            ngb_log("memoria: 0x%08X no pertenece a la imagen del exe en 0x%08X: ¿se lanzó "
                    "ngb-loader.exe enlazado con /BASE:0x10000 /FIXED?", a, NGB_RESERVE_BASE);
            return -1;
        }
        a = (uint32_t)(uintptr_t)m.BaseAddress + (uint32_t)m.RegionSize;
    }
    return 0;
}

int ngb_memory_init(void) {
    DWORD old;
    InitializeCriticalSection(&g_contig_lock);
    if (check_reserve())
        return -1;
    if (!VirtualProtect((void *)(uintptr_t)NGB_RESERVE_BASE,
                        NGB_PE_SHADOW + 0x1000u - NGB_RESERVE_BASE, PAGE_EXECUTE_READWRITE, &old)) {
        ngb_log("memoria: VirtualProtect de la reserva [0x%08X, 0x%08X) falló (%lu)",
                NGB_RESERVE_BASE, NGB_RESERVE_END, GetLastError());
        return -1;
    }
    /* El último MB queda libre: es la RAM de instancias, que gpu/nv2a.c mapea dos veces. */
    g_contig = (uint8_t *)VirtualAlloc((void *)(uintptr_t)NGB_CONTIG_BASE,
                                       NGB_CONTIG_SIZE - 0x100000u,
                                       MEM_RESERVE | MEM_WRITE_WATCH, PAGE_EXECUTE_READWRITE);
    if (!g_contig) {
        g_contig = (uint8_t *)VirtualAlloc(NULL, NGB_CONTIG_SIZE, MEM_RESERVE | MEM_WRITE_WATCH,
                                           PAGE_EXECUTE_READWRITE);
        ngb_log("memoria: 0x%08X ocupado; arena contigua en %p", NGB_CONTIG_BASE, g_contig);
    }
    if (!g_contig)
        return -1;
    fake_kernel_image();
    ngb_log("memoria: imagen [0x%08X, 0x%08X) RWX; arena contigua [%p, +0x%X)", NGB_RESERVE_BASE,
            NGB_RESERVE_END, g_contig, NGB_CONTIG_SIZE);
    return 0;
}

/*
 * Cabecera PE "sombra". La cabecera del XBE pisa la del exe en 0x10000, pero Windows la
 * sigue leyendo directamente de la base de la imagen: sin ella CreateThread falla con
 * ERROR_BAD_EXE_FORMAT y los hilos nuevos mueren al inicializarse en ntdll. Solución:
 *   - las NT headers originales se copian a NGB_PE_SHADOW (cola de la BSS del exe, fuera
 *     del rango del XBE);
 *   - en 0x10000 se escribe "MZ" (pisa "XB" del magic XBEH) y en 0x1003C (e_lfanew, que
 *     cae dentro de la firma RSA del XBE, sin uso en ejecución) el offset a la sombra.
 * El código Xbox no relee el magic ni la firma (solo el kernel al cargar), así que el
 * cambio es invisible para el juego; ver port/README.md.
 */
static uint8_t g_pe_header[0x1000];

static void save_pe_header(void) {
    memcpy(g_pe_header, (const void *)(uintptr_t)NGB_RESERVE_BASE, sizeof g_pe_header);
}

static void install_pe_shadow(void) {
    uint32_t nt = *(const uint32_t *)(g_pe_header + 0x3C);
    uint16_t nsect = *(const uint16_t *)(g_pe_header + nt + 6);
    uint16_t optsize = *(const uint16_t *)(g_pe_header + nt + 20);
    uint32_t size = 24u + optsize + 40u * nsect;
    uint8_t *base = (uint8_t *)(uintptr_t)NGB_RESERVE_BASE;
    memcpy((void *)(uintptr_t)NGB_PE_SHADOW, g_pe_header + nt, size);
    base[0] = 'M';
    base[1] = 'Z';
    *(uint32_t *)(base + 0x3C) = NGB_PE_SHADOW - NGB_RESERVE_BASE;
    ngb_log("memoria: cabecera PE sombra en 0x%08X (%u B); e_lfanew en 0x1003C", NGB_PE_SHADOW,
            size);
}

int ngb_memory_map_xbe(const xbe_t *xbe) {
    uint32_t i, end = xbe->base + xbe->size_of_image;
    if (xbe->base != NGB_RESERVE_BASE || end > NGB_RESERVE_END || end < xbe->base) {
        ngb_log("memoria: imagen [0x%08X, 0x%08X) fuera de la reserva [0x%08X, 0x%08X)",
                xbe->base, end, NGB_RESERVE_BASE, NGB_RESERVE_END);
        return -1;
    }
    save_pe_header();
    /* Pisa la cabecera PE y el código del exe stub: ya no se ejecutan (ver loader_stub.c). */
    memset((void *)(uintptr_t)xbe->base, 0, NGB_RESERVE_END - NGB_RESERVE_BASE);
    memcpy((void *)(uintptr_t)xbe->base, xbe->data, xbe->size_of_headers);
    for (i = 0; i < xbe->section_count; i++) {
        const xbe_section_t *s = &xbe->sections[i];
        uint32_t backed = s->rsize < s->vsize ? s->rsize : s->vsize;
        memcpy((void *)(uintptr_t)s->va, xbe->data + s->raw, backed);
        ngb_log("memoria: sección %-10s [0x%08X, 0x%08X) %6u KB del archivo, flags 0x%02X",
                s->name, s->va, s->va + s->vsize, backed / 1024, s->flags);
    }
    install_pe_shadow();
    FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)xbe->base, xbe->size_of_image);
    return 0;
}

int ngb_memory_patch_thunk(const xbe_t *xbe) {
    uint32_t *slot = (uint32_t *)(uintptr_t)xbe->thunk;
    int nfunc = 0, ndata = 0, nbad = 0;
    for (; *slot; slot++) {
        uint32_t entry = *slot, ordinal = entry & 0x7FFFFFFFu;
        const ngb_kexport_t *e = (entry & 0x80000000u) ? ngb_kexport(ordinal) : NULL;
        if (!e) {
            ngb_log("thunk: slot 0x%08X = 0x%08X sin export conocido", (uint32_t)(uintptr_t)slot,
                    entry);
            nbad++;
            continue;
        }
        if (e->kind == NGB_KIND_FUNC) {
            *slot = (uint32_t)(uintptr_t)e->stub;
            nfunc++;
        } else {
            *slot = (uint32_t)(uintptr_t)ngb_kdata_address(ordinal);
            ndata++;
        }
    }
    ngb_log("thunk: 0x%08X, %d funciones + %d variables enlazadas, %d desconocidas", xbe->thunk,
            nfunc, ndata, nbad);
    return nbad ? -1 : 0;
}

/*
 * Lecturas del IRQL por fs:[0x24]: `64 0F B6 /r 24 00 00 00` (movzx r32, byte ptr fs:[24h]).
 * Como fs:[0x24] sigue siendo el TID de Windows (ver thread.c), se cambian por
 * `mov r32, 0` (B8+r imm32) y 3 NOP: el código Xbox ve PASSIVE_LEVEL y los flags no cambian.
 */
int ngb_memory_patch_irql(const xbe_t *xbe) {
    uint32_t i, n = 0;
    for (i = 0; i < xbe->section_count; i++) {
        const xbe_section_t *s = &xbe->sections[i];
        uint8_t *p = (uint8_t *)(uintptr_t)s->va;
        uint32_t k, len = s->rsize < s->vsize ? s->rsize : s->vsize;
        if (!(s->flags & XBE_SECTION_EXECUTABLE) || (s->flags & XBE_SECTION_INSERTED_FILE) || len < 8)
            continue;
        for (k = 0; k + 8 <= len; k++) {
            uint8_t modrm = p[k + 3];
            if (p[k] != 0x64 || p[k + 1] != 0x0F || p[k + 2] != 0xB6 || (modrm & 0xC7) != 0x05 ||
                p[k + 4] != 0x24 || p[k + 5] || p[k + 6] || p[k + 7])
                continue;
            p[k] = (uint8_t)(0xB8 + ((modrm >> 3) & 7));
            p[k + 1] = p[k + 2] = p[k + 3] = p[k + 4] = 0;
            p[k + 5] = p[k + 6] = p[k + 7] = 0x90;
            ngb_log("memoria: IRQL fs:[0x24] en 0x%08X -> mov r32, 0", s->va + k);
            n++;
            k += 7;
        }
    }
    FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)xbe->base, xbe->size_of_image);
    return (int)n;
}

void *ngb_contig_alloc(uint32_t size, uint32_t align, uint32_t lowest, uint32_t highest) {
    uint32_t base, i;
    void *p = NULL;
    (void)lowest;
    if (!size)
        return NULL;
    if (align < 0x1000)
        align = 0x1000;
    size = (size + 0xFFFu) & ~0xFFFu;
    EnterCriticalSection(&g_contig_lock);
    /* Primero, un hueco liberado del mismo tamaño o mayor que cumpla la alineación. */
    for (i = 0; i < (uint32_t)g_nblocks; i++) {
        contig_block_t *b = &g_blocks[i];
        if (!b->used && b->size >= size && (b->base % align) == 0) {
            b->used = 1;
            p = g_contig + b->base;
            memset(p, 0, b->size);
            goto out;
        }
    }
    base = (g_contig_top + align - 1) & ~(align - 1);
    if ((uint64_t)base + size > NGB_CONTIG_SIZE - 0x100000u || (highest && base + size - 1 > highest) ||
        g_nblocks >= MAX_CONTIG_BLOCKS)
        goto out;
    if (!VirtualAlloc(g_contig + base, size, MEM_COMMIT, PAGE_EXECUTE_READWRITE))
        goto out;
    g_blocks[g_nblocks].base = base;
    g_blocks[g_nblocks].size = size;
    g_blocks[g_nblocks].used = 1;
    g_nblocks++;
    g_contig_top = base + size;
    p = g_contig + base;
out:
    LeaveCriticalSection(&g_contig_lock);
    return p;
}

static contig_block_t *find_block(const void *p) {
    int i;
    uint32_t off;
    if (!ngb_contig_contains(p))
        return NULL;
    off = (uint32_t)((const uint8_t *)p - g_contig);
    for (i = 0; i < g_nblocks; i++)
        if (g_blocks[i].used && off >= g_blocks[i].base && off < g_blocks[i].base + g_blocks[i].size)
            return &g_blocks[i];
    return NULL;
}

int ngb_contig_free(void *p) {
    contig_block_t *b;
    EnterCriticalSection(&g_contig_lock);
    b = find_block(p);
    if (b)
        b->used = 0;
    LeaveCriticalSection(&g_contig_lock);
    return b != NULL;
}

uint32_t ngb_contig_size(const void *p) {
    contig_block_t *b;
    uint32_t size = 0;
    EnterCriticalSection(&g_contig_lock);
    b = find_block(p);
    if (b)
        size = b->size;
    LeaveCriticalSection(&g_contig_lock);
    return size;
}

/* Páginas de la arena escritas desde la última llamada (write watch, H7c): sus offsets
 * físicos van a `phys` (hasta `cap`) y el contador se reinicia. Devuelve cuántas hay, o
 * (size_t)-1 si Windows no da el write watch. El render las usa para no volver a hashear
 * texturas y vértices que la CPU no ha tocado. */
size_t ngb_contig_written(uint32_t *phys, size_t cap) {
    static PVOID addrs[4096];
    size_t total = 0;
    for (;;) {
        ULONG_PTR count = sizeof addrs / sizeof addrs[0];
        ULONG gran = 0;
        ULONG_PTR i;
        if (!g_contig || GetWriteWatch(WRITE_WATCH_FLAG_RESET, g_contig, NGB_CONTIG_SIZE - 0x100000u,
                                       addrs, &count, &gran))
            return (size_t)-1;
        for (i = 0; i < count; i++, total++)
            if (total < cap)
                phys[total] = (uint32_t)((uint8_t *)addrs[i] - g_contig);
        if (count < sizeof addrs / sizeof addrs[0])
            return total;
    }
}

int ngb_contig_contains(const void *p) {
    return g_contig && (const uint8_t *)p >= g_contig && (const uint8_t *)p < g_contig + NGB_CONTIG_SIZE;
}

static int dumpable(const MEMORY_BASIC_INFORMATION *m) {
    uintptr_t at = (uintptr_t)m->BaseAddress;
    if (m->State != MEM_COMMIT || (m->Protect & (PAGE_GUARD | PAGE_NOACCESS)))
        return 0;
    if (!(m->Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY |
                        PAGE_EXECUTE_WRITECOPY)))
        return 0;
    /* The XBE image lives in the loader exe's reserve (MEM_IMAGE); the rest is the game's
       heap and the contiguous RAM (MEM_PRIVATE). Host modules are images outside the reserve. */
    if (m->Type == MEM_IMAGE)
        return at >= NGB_RESERVE_BASE && at < NGB_RESERVE_END;
    return m->Type == MEM_PRIVATE;
}

int ngb_dump_guest_ram(const char *path) {
    static uint8_t chunk[1 << 16];
    MEMORY_BASIC_INFORMATION m;
    uintptr_t at = NGB_RESERVE_BASE, end = (uintptr_t)NGB_CONTIG_BASE + NGB_CONTIG_SIZE;
    size_t total = 0;
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    while (at < end && VirtualQuery((const void *)at, &m, sizeof m)) {
        uintptr_t next = (uintptr_t)m.BaseAddress + m.RegionSize;
        if (dumpable(&m) && m.RegionSize <= (512u << 20)) {
            uintptr_t p = (uintptr_t)m.BaseAddress;
            while (p < next) {
                uint32_t hdr[2];
                SIZE_T want = next - p < sizeof chunk ? next - p : sizeof chunk, got = 0;
                if (!ReadProcessMemory(GetCurrentProcess(), (const void *)p, chunk, want, &got) || !got)
                    break; /* decommitted under our feet: skip the rest of the region */
                hdr[0] = (uint32_t)p;
                hdr[1] = (uint32_t)got;
                fwrite(hdr, sizeof hdr, 1, f);
                fwrite(chunk, 1, got, f);
                total += got;
                p += got;
            }
        }
        if (next <= at)
            break;
        at = next;
    }
    fclose(f);
    ngb_log("    · RAM dump: %u MB -> %s", (unsigned)(total >> 20), path);
    return 0;
}

void ngb_hexdump_line(char *out, size_t outlen, uint32_t va, uint32_t len) {
    uint8_t buf[64];
    SIZE_T got = 0;
    size_t pos = 0;
    uint32_t i;
    if (len > sizeof buf)
        len = sizeof buf;
    out[0] = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), (const void *)(uintptr_t)va, buf, len, &got))
        got = 0;
    for (i = 0; i < len && pos + 4 < outlen; i++) {
        if (i < got)
            pos += (size_t)snprintf(out + pos, outlen - pos, "%02x ", buf[i]);
        else
            pos += (size_t)snprintf(out + pos, outlen - pos, "?? ");
    }
}
