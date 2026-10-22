/*
 * rdoc.h: captura de un frame con RenderDoc (--rdoc-frame N). Ver rdoc.c.
 */
#ifndef NGB_RDOC_H
#define NGB_RDOC_H

#include <stdint.h>

/* Carga la DLL de RenderDoc (antes de crear el contexto GL) si se pide el frame `frame` o la
 * tecla de captura (`hotkey`). 0 si queda lista o no se pidió nada. */
int rdoc_load(uint32_t frame, int hotkey);
/* Tras presentar el frame `presented` (en el hilo de render): empieza o termina la captura. */
void rdoc_after_present(uint32_t presented);

#endif
