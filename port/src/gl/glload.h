/*
 * glload.h: cargador mínimo de funciones de OpenGL 4.x y WGL (sin GLEW ni glad).
 *
 * `opengl32.lib` solo exporta OpenGL 1.1; el resto se pide al driver con
 * `wglGetProcAddress` una vez que hay un contexto actual. Aquí están únicamente las
 * funciones que usa glbackend.c, con los tipos y las constantes que no trae <GL/gl.h>.
 * Valores de las constantes: registro de Khronos (glcorearb.h y wglext.h).
 */
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <GL/gl.h>
#include <stddef.h>

typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;

/* Constantes de OpenGL > 1.1. */
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_TEXTURE0 0x84C0
#define GL_ARRAY_BUFFER 0x8892
#define GL_STREAM_DRAW 0x88E0
#define GL_DEPTH24_STENCIL8 0x88F0
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#define GL_FRAMEBUFFER 0x8D40
#define GL_RENDERBUFFER 0x8D41
#define GL_MAJOR_VERSION 0x821B
#define GL_NUM_EXTENSIONS 0x821D
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_BGRA 0x80E1
#define GL_TEXTURE_BASE_LEVEL 0x813C
#define GL_TEXTURE_MAX_LEVEL 0x813D
#define GL_TEXTURE_CUBE_MAP 0x8513
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
#define GL_MINOR_VERSION 0x821C

/* WGL_ARB_create_context y WGL_ARB_create_context_profile. */
#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_FLAGS_ARB 0x2094
#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001

#define NGL_FUNCTIONS(X)                                                                   \
    X(HGLRC, wglCreateContextAttribsARB, (HDC, HGLRC, const int *))                       \
    X(void, glBlendEquation, (GLenum))                                                    \
    X(void, glBlendColor, (GLfloat, GLfloat, GLfloat, GLfloat))                           \
    X(void, glCompressedTexImage2D,                                                       \
      (GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void *))            \
    X(const GLubyte *, glGetStringi, (GLenum, GLuint))                                    \
    X(void, glActiveTexture, (GLenum))                                                    \
    X(GLuint, glCreateShader, (GLenum))                                                   \
    X(void, glShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *))      \
    X(void, glCompileShader, (GLuint))                                                    \
    X(void, glGetShaderiv, (GLuint, GLenum, GLint *))                                     \
    X(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *))                   \
    X(void, glDeleteShader, (GLuint))                                                     \
    X(GLuint, glCreateProgram, (void))                                                    \
    X(void, glAttachShader, (GLuint, GLuint))                                             \
    X(void, glLinkProgram, (GLuint))                                                      \
    X(void, glGetProgramiv, (GLuint, GLenum, GLint *))                                    \
    X(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *))                  \
    X(void, glDeleteProgram, (GLuint))                                                    \
    X(void, glUseProgram, (GLuint))                                                       \
    X(GLint, glGetUniformLocation, (GLuint, const GLchar *))                              \
    X(void, glUniform1i, (GLint, GLint))                                                  \
    X(void, glUniform3i, (GLint, GLint, GLint, GLint))                                    \
    X(void, glUniform1fv, (GLint, GLsizei, const GLfloat *))                              \
    X(void, glUniform2fv, (GLint, GLsizei, const GLfloat *))                              \
    X(void, glUniform4fv, (GLint, GLsizei, const GLfloat *))                              \
    X(void, glUniformMatrix2fv, (GLint, GLsizei, GLboolean, const GLfloat *))             \
    X(void, glGenVertexArrays, (GLsizei, GLuint *))                                       \
    X(void, glBindVertexArray, (GLuint))                                                  \
    X(void, glDeleteVertexArrays, (GLsizei, const GLuint *))                              \
    X(void, glGenBuffers, (GLsizei, GLuint *))                                            \
    X(void, glBindBuffer, (GLenum, GLuint))                                               \
    X(void, glBufferData, (GLenum, GLsizeiptr, const void *, GLenum))                     \
    X(void, glDeleteBuffers, (GLsizei, const GLuint *))                                   \
    X(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) \
    X(void, glEnableVertexAttribArray, (GLuint))                                          \
    X(void, glDisableVertexAttribArray, (GLuint))                                         \
    X(void, glVertexAttrib4f, (GLuint, GLfloat, GLfloat, GLfloat, GLfloat))               \
    X(void, glGenFramebuffers, (GLsizei, GLuint *))                                       \
    X(void, glBindFramebuffer, (GLenum, GLuint))                                          \
    X(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))              \
    X(void, glFramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint))                  \
    X(GLenum, glCheckFramebufferStatus, (GLenum))                                         \
    X(void, glDeleteFramebuffers, (GLsizei, const GLuint *))                              \
    X(void, glGenRenderbuffers, (GLsizei, GLuint *))                                      \
    X(void, glBindRenderbuffer, (GLenum, GLuint))                                         \
    X(void, glRenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei))                    \
    X(void, glDeleteRenderbuffers, (GLsizei, const GLuint *))                             \
    X(void, glBlitFramebuffer,                                                            \
      (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum))

#define NGL_DECLARE(ret, name, args)                                                       \
    typedef ret(APIENTRY *ngl_pfn_##name) args;                                           \
    extern ngl_pfn_##name ngl_##name;
NGL_FUNCTIONS(NGL_DECLARE)
#undef NGL_DECLARE

/*
 * Carga `wglCreateContextAttribsARB` (fase 0, con el contexto 1.1 temporal) o todas las
 * demás (fase 1, con el contexto 4.x actual). Devuelve cuántas faltan (0 = todo bien) y
 * deja en `missing` el nombre de la primera que falta, si se pasa.
 */
int ngl_load(int phase, const char **missing);
