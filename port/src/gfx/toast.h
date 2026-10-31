/*
 * toast.h: a notice drawn by the port over the game's frame (achievements), like the Steam
 * overlay. The game is not patched: the GL present composites it before SwapBuffers.
 *
 *   toast_show(L"Achievement unlocked", L"Chapter 1 complete");  // any thread
 *   toast_draw(client_w, client_h, game_rect);                   // render thread, GL current
 *
 * The toast slides in at the bottom centre of the game picture, stays about 5 s and fades out.
 * Spike look: GDI text on a dark panel with a vermilion bar; the final frame comes with the
 * achievements design.
 */
#ifndef NGB_TOAST_H
#define NGB_TOAST_H

#include <wchar.h>

void toast_show(const wchar_t *title, const wchar_t *text);
/* `game_rect`: left, top, right, bottom of the game picture in the window (top-left origin). */
void toast_draw(int client_w, int client_h, const int game_rect[4]);

#endif
