/*
 * rdoc.h: single-frame capture with RenderDoc (--rdoc-frame N). See rdoc.c.
 */
#ifndef NGB_RDOC_H
#define NGB_RDOC_H

#include <stdint.h>

/* Loads the RenderDoc DLL (before creating the GL context) if frame `frame` or the capture
 * key (`hotkey`) is requested. 0 if it is ready or nothing was requested. */
int rdoc_load(uint32_t frame, int hotkey);
/* After presenting frame `presented` (on the render thread): starts or ends the capture. */
void rdoc_after_present(uint32_t presented);

#endif
