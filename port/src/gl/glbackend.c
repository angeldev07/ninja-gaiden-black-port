/*
 * glbackend.c: WGL 4.x context, offscreen FBO, GLSL compilation and drawing (glbackend.h).
 *
 * WGL startup takes two steps, because `wglCreateContextAttribsARB` can only be requested
 * with a current context: (1) pixel format on the DC, temporary 1.1 context and loading of
 * that function; (2) core 4.x context (tried from 4.6 down to 4.0, the generated GLSL is
 * #version 400), the temporary one is deleted and the other functions are loaded (glload.c).
 *
 * It always draws into an FBO (RGBA8 color and depth24/stencil8 in renderbuffers), never to
 * the window: this way the result does not depend on the window being visible and
 * glb_read_rgba is exact. glb_present copies it to the window with glBlitFramebuffer.
 */
#include "glbackend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "glload.h"

#define WINDOW_CLASS "ngb_gl_backend"

static struct {
    int ready;
    HWND hwnd;
    int own_window;
    HDC dc;
    HGLRC rc;
    int width, height;
    GLuint fbo, color_rb, depth_rb;
    GLuint vao, vbo, ebo;
    GLuint textures[GLB_MAX_TEXTURES];
    GLenum targets[GLB_MAX_TEXTURES];
    int s3tc;
    glb_info info;
} g;

#define S3TC_EXTENSION "GL_EXT_texture_compression_s3tc"
#define CMP_FLOATS 3

const char *glb_error_text(int code) {
    switch (code) {
    case GLB_ERR_ARGS: return "invalid arguments or backend not initialized";
    case GLB_ERR_WINDOW: return "could not create the window or choose the pixel format";
    case GLB_ERR_CONTEXT: return "no OpenGL 4.x context";
    case GLB_ERR_FUNCTIONS: return "OpenGL 4.x functions are missing";
    case GLB_ERR_FBO: return "the framebuffer is not complete";
    case GLB_ERR_COMPILE: return "the driver rejected the shader";
    case GLB_ERR_GL: return "OpenGL error";
    default: return code >= 0 ? "ok" : "unknown error";
    }
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    return DefWindowProcA(hwnd, msg, wp, lp);
}

/* Icon of resource 1 of this module (ngb-core.dll, res\ngb.rc); NULL if it has none. */
static HICON load_icon(void) {
    HMODULE mod = NULL;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)&g, &mod) ||
        !mod) {
        return NULL;
    }
    return (HICON)LoadImageA(mod, MAKEINTRESOURCEA(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                             GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
}

static int create_window(int width, int height) {
    WNDCLASSA wc;
    RECT rect;
    POINT cursor;
    int x, y;
    HINSTANCE instance = GetModuleHandleA(NULL);
    memset(&wc, 0, sizeof wc);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hIcon = load_icon();
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    wc.lpszClassName = WINDOW_CLASS;
    if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return GLB_ERR_WINDOW;
    }
    rect.left = 0;
    rect.top = 0;
    rect.right = width;
    rect.bottom = height;
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    /* Centered in the work area of the monitor where the cursor is. */
    x = CW_USEDEFAULT;
    y = CW_USEDEFAULT;
    if (GetCursorPos(&cursor)) {
        MONITORINFO mi;
        HMONITOR mon = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
        memset(&mi, 0, sizeof mi);
        mi.cbSize = sizeof mi;
        if (mon && GetMonitorInfoA(mon, &mi)) {
            x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - (rect.right - rect.left)) / 2;
            y = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - (rect.bottom - rect.top)) / 2;
            if (x < mi.rcWork.left) {
                x = mi.rcWork.left;
            }
            if (y < mi.rcWork.top) {
                y = mi.rcWork.top;
            }
        }
    }
    g.hwnd = CreateWindowExA(0, WINDOW_CLASS, "Ninja Gaiden Black - native port",
                             WS_OVERLAPPEDWINDOW, x, y, rect.right - rect.left,
                             rect.bottom - rect.top, NULL, NULL, instance, NULL);
    if (!g.hwnd) {
        return GLB_ERR_WINDOW;
    }
    g.own_window = 1;
    return 0;
}

static int set_pixel_format(HDC dc) {
    PIXELFORMATDESCRIPTOR pfd;
    int format;
    if (GetPixelFormat(dc) != 0) {
        return 0; /* the caller's window already has one: it is respected */
    }
    memset(&pfd, 0, sizeof pfd);
    pfd.nSize = sizeof pfd;
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cAlphaBits = 8;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;
    format = ChoosePixelFormat(dc, &pfd);
    if (!format || !SetPixelFormat(dc, format, &pfd)) {
        return GLB_ERR_WINDOW;
    }
    return 0;
}

static int create_context(void) {
    HGLRC temp = wglCreateContext(g.dc);
    const char *missing = NULL;
    int minor;
    if (!temp || !wglMakeCurrent(g.dc, temp)) {
        if (temp) {
            wglDeleteContext(temp);
        }
        return GLB_ERR_CONTEXT;
    }
    if (ngl_load(0, &missing) != 0) {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(temp);
        return GLB_ERR_CONTEXT;
    }
    for (minor = 6; minor >= 0 && !g.rc; minor--) {
        int attribs[7];
        attribs[0] = WGL_CONTEXT_MAJOR_VERSION_ARB;
        attribs[1] = 4;
        attribs[2] = WGL_CONTEXT_MINOR_VERSION_ARB;
        attribs[3] = minor;
        attribs[4] = WGL_CONTEXT_PROFILE_MASK_ARB;
        attribs[5] = WGL_CONTEXT_CORE_PROFILE_BIT_ARB;
        attribs[6] = 0;
        g.rc = ngl_wglCreateContextAttribsARB(g.dc, NULL, attribs);
    }
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(temp);
    if (!g.rc || !wglMakeCurrent(g.dc, g.rc)) {
        return GLB_ERR_CONTEXT;
    }
    if (ngl_load(1, &missing) != 0) {
        return GLB_ERR_FUNCTIONS;
    }
    return 0;
}

static void copy_string(char *dst, size_t len, GLenum name) {
    const GLubyte *text = glGetString(name);
    snprintf(dst, len, "%s", text ? (const char *)text : "?");
}

static void read_info(void) {
    copy_string(g.info.vendor, sizeof g.info.vendor, GL_VENDOR);
    copy_string(g.info.renderer, sizeof g.info.renderer, GL_RENDERER);
    copy_string(g.info.version, sizeof g.info.version, GL_VERSION);
    copy_string(g.info.glsl_version, sizeof g.info.glsl_version, GL_SHADING_LANGUAGE_VERSION);
    glGetIntegerv(GL_MAJOR_VERSION, &g.info.major);
    glGetIntegerv(GL_MINOR_VERSION, &g.info.minor);
}

static void detect_extensions(void) {
    GLint count = 0, k;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (k = 0; k < count; k++) {
        const GLubyte *name = ngl_glGetStringi(GL_EXTENSIONS, (GLuint)k);
        if (name && !strcmp((const char *)name, S3TC_EXTENSION)) {
            g.s3tc = 1;
        }
    }
}

int glb_has_s3tc(void) {
    return g.ready && g.s3tc;
}

static int create_targets(void) {
    ngl_glGenRenderbuffers(1, &g.color_rb);
    ngl_glBindRenderbuffer(GL_RENDERBUFFER, g.color_rb);
    ngl_glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, g.width, g.height);
    ngl_glGenRenderbuffers(1, &g.depth_rb);
    ngl_glBindRenderbuffer(GL_RENDERBUFFER, g.depth_rb);
    ngl_glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, g.width, g.height);
    ngl_glGenFramebuffers(1, &g.fbo);
    ngl_glBindFramebuffer(GL_FRAMEBUFFER, g.fbo);
    ngl_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, g.color_rb);
    ngl_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER,
                                  g.depth_rb);
    if (ngl_glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        return GLB_ERR_FBO;
    }
    ngl_glGenVertexArrays(1, &g.vao);
    ngl_glGenBuffers(1, &g.vbo);
    ngl_glGenBuffers(1, &g.ebo);
    glViewport(0, 0, g.width, g.height);
    return glGetError() == GL_NO_ERROR ? 0 : GLB_ERR_GL;
}

int glb_init(HWND hwnd, int width, int height) {
    int err;
    if (g.ready) {
        return GLB_ERR_ARGS;
    }
    memset(&g, 0, sizeof g);
    g.width = width > 0 ? width : GLB_DEFAULT_WIDTH;
    g.height = height > 0 ? height : GLB_DEFAULT_HEIGHT;
    if (hwnd) {
        g.hwnd = hwnd;
    } else if ((err = create_window(g.width, g.height)) != 0) {
        return err;
    }
    g.dc = GetDC(g.hwnd);
    err = g.dc ? set_pixel_format(g.dc) : GLB_ERR_WINDOW;
    if (!err) {
        err = create_context();
    }
    if (!err) {
        read_info();
        detect_extensions();
        err = create_targets();
    }
    if (err) {
        g.ready = 1; /* so that glb_shutdown releases what was created */
        glb_shutdown();
        return err;
    }
    g.ready = 1;
    return 0;
}

HDC glb_get_dc(void) { return g.ready ? g.dc : NULL; }

const glb_info *glb_get_info(void) {
    return g.ready ? &g.info : NULL;
}

static void copy_log(char *log, size_t loglen, const char *prefix, const char *text) {
    size_t used;
    if (!log || !loglen) {
        return;
    }
    used = strlen(log);
    if (used + 1 < loglen) {
        snprintf(log + used, loglen - used, "%s%s", prefix, text);
    }
}

static GLuint compile_shader(GLenum type, const char *source, char *log, size_t loglen) {
    GLuint shader = ngl_glCreateShader(type);
    GLint ok = 0, length = 0;
    ngl_glShaderSource(shader, 1, &source, NULL);
    ngl_glCompileShader(shader);
    ngl_glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char text[4096];
        ngl_glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        text[0] = 0;
        ngl_glGetShaderInfoLog(shader, (GLsizei)sizeof text, NULL, text);
        copy_log(log, loglen, type == GL_VERTEX_SHADER ? "[vertex] " : "[fragment] ", text);
        ngl_glDeleteShader(shader);
        return 0;
    }
    return shader;
}

int glb_compile_stage(int fragment, const char *source, char *log, size_t loglen) {
    GLuint shader;
    if (log && loglen) {
        log[0] = 0;
    }
    if (!g.ready || !source) {
        return GLB_ERR_ARGS;
    }
    shader = compile_shader(fragment ? GL_FRAGMENT_SHADER : GL_VERTEX_SHADER, source, log, loglen);
    if (!shader) {
        return GLB_ERR_COMPILE;
    }
    ngl_glDeleteShader(shader);
    return 0;
}

int glb_compile(const char *vs, const char *fs, unsigned *program, char *log, size_t loglen) {
    GLuint vshader, fshader = 0, prog;
    GLint ok = 0;
    if (log && loglen) {
        log[0] = 0;
    }
    if (!g.ready || !vs || !program) {
        return GLB_ERR_ARGS;
    }
    *program = 0;
    vshader = compile_shader(GL_VERTEX_SHADER, vs, log, loglen);
    if (fs) {
        fshader = compile_shader(GL_FRAGMENT_SHADER, fs, log, loglen);
    }
    if (!vshader || (fs && !fshader)) {
        if (vshader) ngl_glDeleteShader(vshader);
        if (fshader) ngl_glDeleteShader(fshader);
        return GLB_ERR_COMPILE;
    }
    prog = ngl_glCreateProgram();
    ngl_glAttachShader(prog, vshader);
    if (fshader) {
        ngl_glAttachShader(prog, fshader);
    }
    ngl_glLinkProgram(prog);
    ngl_glDeleteShader(vshader);
    if (fshader) {
        ngl_glDeleteShader(fshader);
    }
    ngl_glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char text[4096];
        text[0] = 0;
        ngl_glGetProgramInfoLog(prog, (GLsizei)sizeof text, NULL, text);
        copy_log(log, loglen, "[link] ", text);
        ngl_glDeleteProgram(prog);
        return GLB_ERR_COMPILE;
    }
    *program = prog;
    return 0;
}

void glb_delete_program(unsigned program) {
    if (g.ready && program) {
        ngl_glDeleteProgram(program);
    }
}

/* The unit's texture object for `target`; a new one if it had another type. */
static GLuint texture_for(int unit, GLenum target) {
    if (g.textures[unit] && g.targets[unit] != target) {
        glDeleteTextures(1, &g.textures[unit]);
        g.textures[unit] = 0;
    }
    if (!g.textures[unit]) {
        glGenTextures(1, &g.textures[unit]);
    }
    g.targets[unit] = target;
    return g.textures[unit];
}

int glb_set_texture(int unit, int width, int height, const uint8_t *rgba, int linear) {
    GLint filter = linear ? GL_LINEAR : GL_NEAREST;
    if (!g.ready || unit < 0 || unit >= GLB_MAX_TEXTURES || width < 1 || height < 1 || !rgba) {
        return GLB_ERR_ARGS;
    }
    ngl_glActiveTexture(GL_TEXTURE0 + (GLenum)unit);
    glBindTexture(GL_TEXTURE_2D, texture_for(unit, GL_TEXTURE_2D));
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    /* The first uploaded row is t = 0: with D3D UVs (v = 0 at the top) there is no need to flip. */
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    return glGetError() == GL_NO_ERROR ? 0 : GLB_ERR_GL;
}

int glb_clear(float r, float gr, float b, float a) {
    if (!g.ready) {
        return GLB_ERR_ARGS;
    }
    ngl_glBindFramebuffer(GL_FRAMEBUFFER, g.fbo);
    glViewport(0, 0, g.width, g.height);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glStencilMask(0xFFu);
    glClearColor(r, gr, b, a);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    return glGetError() == GL_NO_ERROR ? 0 : GLB_ERR_GL;
}

static void set_uniform(GLuint program, const glb_uniform *u) {
    GLint loc;
    if (!u->name) {
        return;
    }
    loc = ngl_glGetUniformLocation(program, u->name);
    if (loc < 0) {
        return; /* the shader does not use it (or the driver optimized it out) */
    }
    switch (u->kind) {
    case GLB_U_FLOAT: ngl_glUniform1fv(loc, u->count, u->values); break;
    case GLB_U_VEC2: ngl_glUniform2fv(loc, u->count, u->values); break;
    case GLB_U_VEC4: ngl_glUniform4fv(loc, u->count, u->values); break;
    case GLB_U_MAT2: ngl_glUniformMatrix2fv(loc, u->count, GL_FALSE, u->values); break;
    case GLB_U_INT: ngl_glUniform1i(loc, u->ivalue); break;
    }
}

static GLenum prim_mode(glb_prim prim) {
    switch (prim) {
    case GLB_POINTS: return GL_POINTS;
    case GLB_LINES: return GL_LINES;
    case GLB_LINE_STRIP: return GL_LINE_STRIP;
    case GLB_LINE_LOOP: return GL_LINE_LOOP;
    case GLB_TRIANGLE_STRIP:
    case GLB_QUAD_STRIP: return GL_TRIANGLE_STRIP;
    case GLB_TRIANGLE_FAN:
    case GLB_POLYGON: return GL_TRIANGLE_FAN;
    default: return GL_TRIANGLES; /* TRIANGLES and QUADS (already split) */
    }
}

int glb_prim_from_nv2a(uint32_t op, glb_prim *out) {
    /* NV097_SET_BEGIN_END_OP_*: 1 POINTS ... 10 POLYGON (0 is END). */
    static const glb_prim map[11] = {
        GLB_POINTS, GLB_POINTS, GLB_LINES, GLB_LINE_LOOP, GLB_LINE_STRIP, GLB_TRIANGLES,
        GLB_TRIANGLE_STRIP, GLB_TRIANGLE_FAN, GLB_QUADS, GLB_QUAD_STRIP, GLB_POLYGON,
    };
    if (!out || op < 1 || op > 10) {
        return GLB_ERR_ARGS;
    }
    *out = map[op];
    return 0;
}

void glb_decode_cmp(uint32_t packed, float out[3]) {
    /* x: bits 0-10, y: 11-21 (11 signed bits), z: 22-31 (10 signed bits). */
    int32_t x = (int32_t)(packed << 21) >> 21;
    int32_t y = (int32_t)(packed << 10) >> 21;
    int32_t z = (int32_t)packed >> 22;
    out[0] = (float)x / 1023.0f;
    out[1] = (float)y / 1023.0f;
    out[2] = (float)z / 511.0f;
}

int glb_attrib_from_nv2a(int input, uint32_t array_format, uint32_t offset, glb_attrib *out) {
    uint32_t type = array_format & 0xFu, size = (array_format >> 4) & 0xFu;
    if (!out || input < 0 || input >= GLB_MAX_INPUTS) {
        return GLB_ERR_ARGS;
    }
    memset(out, 0, sizeof *out);
    if (size == 0) {
        return 1; /* disabled attribute */
    }
    out->input = input;
    out->components = (int)size;
    out->offset = (int)offset;
    out->stride = (int)(array_format >> 8);
    switch (type) {
    case 0: out->type = GLB_ATTR_UB_D3D; break;
    case 1: out->type = GLB_ATTR_S1; break;
    case 2: out->type = GLB_ATTR_FLOAT; break;
    case 4: out->type = GLB_ATTR_UB_OGL; break;
    case 5: out->type = GLB_ATTR_S32K; break;
    case 6: out->type = GLB_ATTR_CMP; out->components = CMP_FLOATS; break;
    default: return GLB_ERR_ARGS;
    }
    if (out->components > 4 || (out->type == GLB_ATTR_UB_D3D && out->components != 4)) {
        return GLB_ERR_ARGS;
    }
    return 0;
}

static int attrib_bytes(const glb_attrib *a) {
    switch (a->type) {
    case GLB_ATTR_UB_D3D:
    case GLB_ATTR_UB_OGL: return a->components;
    case GLB_ATTR_S1:
    case GLB_ATTR_S32K: return 2 * a->components;
    case GLB_ATTR_CMP: return 4;
    default: return 4 * a->components;
    }
}

static int attrib_stride(const glb_format *fmt, const glb_attrib *a) {
    return a->stride > 0 ? a->stride : fmt->stride;
}

/* Checks the format and that `n_vertices` vertices of each attribute fit in the buffer. */
static int valid_format(const glb_format *fmt, size_t vertex_bytes, size_t n_vertices) {
    int k;
    if (!fmt || fmt->stride <= 0 || fmt->n_attribs < 0 || fmt->n_attribs > GLB_MAX_INPUTS) {
        return 0;
    }
    for (k = 0; k < fmt->n_attribs; k++) {
        const glb_attrib *a = &fmt->attribs[k];
        size_t stride = (size_t)attrib_stride(fmt, a);
        if (a->input < 0 || a->input >= GLB_MAX_INPUTS || a->components < 1 ||
            a->components > 4 || a->offset < 0 || a->type < GLB_ATTR_FLOAT ||
            a->type > GLB_ATTR_CMP || (a->type == GLB_ATTR_UB_D3D && a->components != 4) ||
            (a->type == GLB_ATTR_CMP && a->components != CMP_FLOATS)) {
            return 0;
        }
        if (n_vertices &&
            (size_t)a->offset + stride * (n_vertices - 1) + (size_t)attrib_bytes(a) > vertex_bytes) {
            return 0;
        }
    }
    return 1;
}

static void attrib_pointer(const glb_attrib *a, GLsizei stride, size_t offset) {
    const void *ptr = (const void *)(INT_PTR)offset;
    GLuint index = (GLuint)a->input;
    switch (a->type) {
    case GLB_ATTR_UB_D3D:
        ngl_glVertexAttribPointer(index, GL_BGRA, GL_UNSIGNED_BYTE, GL_TRUE, stride, ptr);
        break;
    case GLB_ATTR_UB_OGL:
        ngl_glVertexAttribPointer(index, a->components, GL_UNSIGNED_BYTE, GL_TRUE, stride, ptr);
        break;
    case GLB_ATTR_S1:
        ngl_glVertexAttribPointer(index, a->components, GL_SHORT, GL_TRUE, stride, ptr);
        break;
    case GLB_ATTR_S32K:
        ngl_glVertexAttribPointer(index, a->components, GL_SHORT, GL_FALSE, stride, ptr);
        break;
    default:
        ngl_glVertexAttribPointer(index, a->components, GL_FLOAT, GL_FALSE, stride, ptr);
        break;
    }
}

static uint32_t index_at(const glb_draw_call *c, int i) {
    if (c->index_size == 2) {
        return ((const uint16_t *)c->indices)[i];
    }
    return ((const uint32_t *)c->indices)[i];
}

/* QUADS -> triangles (0 1 2, 0 2 3) as a u32 index list (through the indices if there are any). */
static uint32_t *split_quads(const glb_draw_call *c, int *n_out) {
    int quads = c->count / 4, q;
    uint32_t *out = (uint32_t *)malloc(sizeof(uint32_t) * 6 * (size_t)(quads ? quads : 1));
    static const int pattern[6] = {0, 1, 2, 0, 2, 3};
    if (!out) {
        return NULL;
    }
    for (q = 0; q < quads; q++) {
        int k;
        for (k = 0; k < 6; k++) {
            int i = 4 * q + pattern[k];
            out[6 * q + k] = c->indices ? index_at(c, i) : (uint32_t)i;
        }
    }
    *n_out = 6 * quads;
    return out;
}

/* The buffer that gets uploaded: the vertices and, after them, the CMPs already converted to float. */
static uint8_t *build_vertex_buffer(const glb_draw_call *c, size_t n_vertices, size_t *total,
                                    size_t cmp_offset[GLB_MAX_INPUTS]) {
    const glb_format *fmt = c->format;
    size_t size = c->vertex_bytes;
    uint8_t *buf;
    int k;
    for (k = 0; k < fmt->n_attribs; k++) {
        if (fmt->attribs[k].type == GLB_ATTR_CMP) {
            cmp_offset[k] = size;
            size += n_vertices * sizeof(float) * CMP_FLOATS;
        }
    }
    buf = (uint8_t *)malloc(size ? size : 1);
    if (!buf) {
        return NULL;
    }
    memcpy(buf, c->vertices, c->vertex_bytes);
    for (k = 0; k < fmt->n_attribs; k++) {
        const glb_attrib *a = &fmt->attribs[k];
        size_t v, stride = (size_t)attrib_stride(fmt, a);
        if (a->type != GLB_ATTR_CMP) {
            continue;
        }
        for (v = 0; v < n_vertices; v++) {
            const uint8_t *p = (const uint8_t *)c->vertices + (size_t)a->offset + stride * v;
            uint32_t packed = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
                              ((uint32_t)p[3] << 24);
            float xyz[CMP_FLOATS];
            glb_decode_cmp(packed, xyz);
            memcpy(buf + cmp_offset[k] + v * sizeof xyz, xyz, sizeof xyz);
        }
    }
    *total = size;
    return buf;
}

int glb_draw_ex(const glb_draw_call *c) {
    size_t n_vertices, total = 0, cmp_offset[GLB_MAX_INPUTS];
    uint8_t *buffer;
    uint32_t *quad_indices = NULL;
    int k, n_draw;
    if (!g.ready || !c || !c->program || !c->vertices || c->count < 1 ||
        (c->n_uniforms > 0 && !c->uniforms) ||
        (c->indices && c->index_size != 2 && c->index_size != 4)) {
        return GLB_ERR_ARGS;
    }
    if (c->indices) {
        uint32_t top = 0;
        for (k = 0; k < c->count; k++) {
            uint32_t i = index_at(c, k);
            top = i > top ? i : top;
        }
        n_vertices = (size_t)top + 1;
    } else {
        n_vertices = (size_t)c->count;
    }
    if (!valid_format(c->format, c->vertex_bytes, n_vertices)) {
        return GLB_ERR_ARGS;
    }
    memset(cmp_offset, 0, sizeof cmp_offset);
    buffer = build_vertex_buffer(c, n_vertices, &total, cmp_offset);
    if (!buffer) {
        return GLB_ERR_ARGS;
    }
    ngl_glBindFramebuffer(GL_FRAMEBUFFER, g.fbo);
    glViewport(0, 0, g.width, g.height);
    ngl_glUseProgram(c->program);
    for (k = 0; k < c->n_uniforms; k++) {
        set_uniform(c->program, &c->uniforms[k]);
    }
    for (k = 0; k < GLB_MAX_TEXTURES; k++) {
        if (g.textures[k]) {
            ngl_glActiveTexture(GL_TEXTURE0 + (GLenum)k);
            glBindTexture(g.targets[k], g.textures[k]);
        }
    }
    ngl_glBindVertexArray(g.vao);
    ngl_glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
    ngl_glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)total, buffer, GL_STREAM_DRAW);
    free(buffer);
    for (k = 0; k < GLB_MAX_INPUTS; k++) {
        ngl_glDisableVertexAttribArray((GLuint)k);
        ngl_glVertexAttrib4f((GLuint)k, 0.0f, 0.0f, 0.0f, 1.0f);
    }
    for (k = 0; k < c->format->n_attribs; k++) {
        const glb_attrib *a = &c->format->attribs[k];
        ngl_glEnableVertexAttribArray((GLuint)a->input);
        if (a->type == GLB_ATTR_CMP) {
            attrib_pointer(a, (GLsizei)(sizeof(float) * CMP_FLOATS), cmp_offset[k]);
        } else {
            attrib_pointer(a, (GLsizei)attrib_stride(c->format, a), (size_t)a->offset);
        }
    }
    if (c->prim == GLB_QUADS) {
        quad_indices = split_quads(c, &n_draw);
        if (!quad_indices) {
            return GLB_ERR_ARGS;
        }
        ngl_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.ebo);
        ngl_glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(sizeof(uint32_t) * (size_t)n_draw),
                         quad_indices, GL_STREAM_DRAW);
        free(quad_indices);
        glDrawElements(GL_TRIANGLES, n_draw, GL_UNSIGNED_INT, NULL);
    } else if (c->indices) {
        ngl_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.ebo);
        ngl_glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)c->index_size * c->count,
                         c->indices, GL_STREAM_DRAW);
        glDrawElements(prim_mode(c->prim), c->count,
                       c->index_size == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, NULL);
    } else {
        glDrawArrays(prim_mode(c->prim), 0, c->count);
    }
    ngl_glBindVertexArray(0);
    ngl_glUseProgram(0);
    return glGetError() == GL_NO_ERROR ? 0 : GLB_ERR_GL;
}

int glb_draw(unsigned program, const void *vertices, const glb_format *fmt, glb_prim prim,
             int count, const glb_uniform *uniforms, int n_uniforms) {
    glb_draw_call c;
    if (!fmt || fmt->stride <= 0 || count < 1) {
        return GLB_ERR_ARGS;
    }
    memset(&c, 0, sizeof c);
    c.program = program;
    c.vertices = vertices;
    c.vertex_bytes = (size_t)fmt->stride * (size_t)count;
    c.format = fmt;
    c.prim = prim;
    c.count = count;
    c.uniforms = uniforms;
    c.n_uniforms = n_uniforms;
    return glb_draw_ex(&c);
}

int glb_upload_texture(int unit, const texconv_desc *d, const uint8_t *data, int linear) {
    GLenum target;
    GLint min_filter, mag_filter = linear ? GL_LINEAR : GL_NEAREST;
    int face, k;
    if (!g.ready || unit < 0 || unit >= GLB_MAX_TEXTURES || !d || !data || d->levels < 1 ||
        (d->upload_compressed && !g.s3tc)) {
        return GLB_ERR_ARGS;
    }
    target = d->faces == TEXCONV_CUBE_FACES ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
    if (d->levels > 1) {
        min_filter = linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST;
    } else {
        min_filter = mag_filter;
    }
    ngl_glActiveTexture(GL_TEXTURE0 + (GLenum)unit);
    glBindTexture(target, texture_for(unit, target));
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (face = 0; face < d->faces; face++) {
        GLenum image = target == GL_TEXTURE_CUBE_MAP ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)face
                                                     : GL_TEXTURE_2D;
        for (k = 0; k < d->levels; k++) {
            const texconv_level *lv = &d->level[k];
            const uint8_t *pixels = data + d->face_dst_stride * (size_t)face + lv->dst_offset;
            if (d->upload_compressed) {
                ngl_glCompressedTexImage2D(image, k, d->gl_internal, lv->width, lv->height, 0,
                                           (GLsizei)lv->dst_size, pixels);
            } else {
                glTexImage2D(image, k, (GLint)d->gl_internal, lv->width, lv->height, 0,
                             d->gl_format, d->gl_type, pixels);
            }
        }
    }
    glTexParameteri(target, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, d->levels - 1);
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, min_filter);
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, mag_filter);
    glTexParameteri(target, GL_TEXTURE_WRAP_S, target == GL_TEXTURE_2D ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, target == GL_TEXTURE_2D ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    return glGetError() == GL_NO_ERROR ? 0 : GLB_ERR_GL;
}

int glb_read_rgba(uint8_t *buf, size_t buflen) {
    size_t row = (size_t)g.width * 4;
    int y;
    if (!g.ready || !buf || buflen < row * (size_t)g.height) {
        return GLB_ERR_ARGS;
    }
    ngl_glBindFramebuffer(GL_FRAMEBUFFER, g.fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glFinish();
    glReadPixels(0, 0, g.width, g.height, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    /* OpenGL gives the bottom row first: it is flipped to get the image top to bottom. */
    for (y = 0; y < g.height / 2; y++) {
        uint8_t *a = buf + row * (size_t)y;
        uint8_t *b = buf + row * (size_t)(g.height - 1 - y);
        size_t k;
        for (k = 0; k < row; k++) {
            uint8_t t = a[k];
            a[k] = b[k];
            b[k] = t;
        }
    }
    return glGetError() == GL_NO_ERROR ? 0 : GLB_ERR_GL;
}

int glb_present(void) {
    RECT client;
    if (!g.ready) {
        return GLB_ERR_ARGS;
    }
    GetClientRect(g.hwnd, &client);
    ngl_glBindFramebuffer(GL_READ_FRAMEBUFFER, g.fbo);
    ngl_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    ngl_glBlitFramebuffer(0, 0, g.width, g.height, 0, 0, client.right, client.bottom,
                          GL_COLOR_BUFFER_BIT, GL_LINEAR);
    SwapBuffers(g.dc);
    ngl_glBindFramebuffer(GL_FRAMEBUFFER, g.fbo);
    return glGetError() == GL_NO_ERROR ? 0 : GLB_ERR_GL;
}

void glb_show_window(int show) {
    if (g.ready && g.own_window) {
        ShowWindow(g.hwnd, show ? SW_SHOW : SW_HIDE);
        UpdateWindow(g.hwnd);
    }
}

void glb_shutdown(void) {
    if (!g.ready) {
        return;
    }
    if (g.rc && wglGetCurrentContext() == g.rc) {
        int k;
        for (k = 0; k < GLB_MAX_TEXTURES; k++) {
            if (g.textures[k]) {
                glDeleteTextures(1, &g.textures[k]);
            }
        }
        if (g.vbo) ngl_glDeleteBuffers(1, &g.vbo);
        if (g.ebo) ngl_glDeleteBuffers(1, &g.ebo);
        if (g.vao) ngl_glDeleteVertexArrays(1, &g.vao);
        if (g.fbo) ngl_glDeleteFramebuffers(1, &g.fbo);
        if (g.color_rb) ngl_glDeleteRenderbuffers(1, &g.color_rb);
        if (g.depth_rb) ngl_glDeleteRenderbuffers(1, &g.depth_rb);
    }
    wglMakeCurrent(NULL, NULL);
    if (g.rc) {
        wglDeleteContext(g.rc);
    }
    if (g.dc) {
        ReleaseDC(g.hwnd, g.dc);
    }
    if (g.own_window && g.hwnd) {
        DestroyWindow(g.hwnd);
    }
    memset(&g, 0, sizeof g);
}
